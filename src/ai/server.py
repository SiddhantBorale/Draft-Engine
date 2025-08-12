# server.py
# pip install fastapi uvicorn opencv-python-headless numpy python-multipart
from fastapi import FastAPI, File, UploadFile, Form
from fastapi.responses import JSONResponse
import uvicorn
import numpy as np
import cv2 as cv
import math

app = FastAPI(title="DraftAI Vectoriser")

# ---------- helpers to build DraftAI JSON ----------
def color_hex(a, r, g, b):
    return f"#{a:02x}{r:02x}{g:02x}{b:02x}"

def make_line(x1,y1,x2,y2, layer=0, color="#ff000000", width=1.0):
    return {
        "type":"line",
        "x1": float(x1), "y1": float(y1),
        "x2": float(x2), "y2": float(y2),
        "color": color, "width": float(width),
        "layer": int(layer)
    }

def make_polygon(points, layer=0, stroke="#ff000000", width=1.0, fill="#00000000", fillStyle=0):
    return {
        "type":"polygon",
        "points":[{"x":float(x), "y":float(y)} for (x,y) in points],
        "color": stroke, "width": float(width),
        "fill": fill, "fillStyle": int(fillStyle),
        "layer": int(layer)
    }

# ---------- image ops ----------
def to_gray(img):
    if img.ndim == 3 and img.shape[2] == 4:
        # premultiply alpha onto white background
        b,g,r,a = cv.split(img)
        a = a.astype(np.float32)/255.0
        for ch in (b,g,r):
            ch[:] = (ch.astype(np.float32)*a + 255*(1.0-a)).astype(np.uint8)
        img = cv.merge([b,g,r])
    return cv.cvtColor(img, cv.COLOR_BGR2GRAY)

def auto_deskew(gray, canny1=80, canny2=160):
    edges = cv.Canny(gray, canny1, canny2)
    lines = cv.HoughLines(edges, 1, np.pi/180.0, 180)
    if lines is None:
        return gray, np.eye(2,3,dtype=np.float32)  # no change
    # collect angles around multiples of 90°
    angs = []
    for rho_theta in lines[:2000,0,:]:
        theta = rho_theta[1]
        deg = (theta*180.0/np.pi) % 180.0
        if deg > 90: deg -= 180
        # map near-vertical to 90 multiples too
        if abs(deg) < 45: pass
        else: deg = deg - 90 if deg > 0 else deg + 90
        angs.append(deg)
    if not angs:
        return gray, np.eye(2,3,dtype=np.float32)
    angle = np.median(angs)
    if abs(angle) < 0.3:  # already straight
        return gray, np.eye(2,3,dtype=np.float32)
    h, w = gray.shape[:2]
    M = cv.getRotationMatrix2D((w/2, h/2), angle, 1.0)
    rotated = cv.warpAffine(gray, M, (w, h), flags=cv.INTER_LINEAR, borderMode=cv.BORDER_REPLICATE)
    return rotated, M

def remove_small_blobs(mask, min_area=180, max_aspect_keep_long=6.0):
    # remove very small almost-square blobs (labels, dots),
    # keep elongated blobs (likely line fragments)
    num, lbl, stats, _ = cv.connectedComponentsWithStats(mask, connectivity=8)
    out = np.zeros_like(mask)
    for i in range(1, num):
        x,y,w,h,area = stats[i]
        if area < min_area:
            # keep if very elongated (likely line bits)
            aspect = max(w,h)/max(1,min(w,h))
            if aspect >= max_aspect_keep_long:
                out[lbl==i] = 255
            continue
        out[lbl==i] = 255
    return out

def close_gaps(mask, radius=3):
    k = cv.getStructuringElement(cv.MORPH_RECT, (2*radius+1, 2*radius+1))
    closed = cv.morphologyEx(mask, cv.MORPH_CLOSE, k, iterations=1)
    return closed

def merge_collinear_segments(segments, angle_tol_deg=6.0, dist_tol=6.0, min_len=30):
    # segments: Nx4 array [x1,y1,x2,y2]
    if len(segments) == 0:
        return []
    segs = [list(map(float, s)) for s in segments]
    used = [False]*len(segs)
    merged = []

    def seg_angle(s):
        a = math.degrees(math.atan2(s[3]-s[1], s[2]-s[0]))
        a = (a+180.0) % 180.0
        if a > 90.0: a -= 180.0
        return a

    def length(s):
        return math.hypot(s[2]-s[0], s[3]-s[1])

    def near_collinear(a1, a2):
        return abs(((a1-a2+90)%180)-90) <= angle_tol_deg

    def point_line_dist(px,py, s):
        # distance from p to infinite line through s
        x1,y1,x2,y2 = s
        A = y2 - y1
        B = x1 - x2
        C = x2*y1 - x1*y2
        return abs(A*px + B*py + C)/max(1e-6, math.hypot(A,B))

    for i, s in enumerate(segs):
        if used[i]: continue
        used[i] = True
        a = seg_angle(s)
        x1,y1,x2,y2 = s
        changed = True
        while changed:
            changed = False
            for j,t in enumerate(segs):
                if used[j]: continue
                b = seg_angle(t)
                if not near_collinear(a,b): continue
                # endpoints near the other segment's line?
                d11 = point_line_dist(x1,y1,t)
                d12 = point_line_dist(x2,y2,t)
                d21 = point_line_dist(t[0],t[1],s)
                d22 = point_line_dist(t[2],t[3],s)
                if min(d11,d12,d21,d22) > dist_tol: 
                    continue
                # try to extend by taking extreme projections along direction
                pts = np.array([[x1,y1],[x2,y2],[t[0],t[1]],[t[2],t[3]]], dtype=np.float32)
                # direction as unit vector
                vx, vy = math.cos(math.radians(a)), math.sin(math.radians(a))
                proj = pts @ np.array([vx,vy], dtype=np.float32)
                i_min, i_max = int(np.argmin(proj)), int(np.argmax(proj))
                x1,y1 = pts[i_min].tolist()
                x2,y2 = pts[i_max].tolist()
                s = [x1,y1,x2,y2]
                used[j] = True
                changed = True
        if length(s) >= min_len:
            merged.append([x1,y1,x2,y2])
    return merged

# ---------- main vectoriser ----------
def vectorise(img_bgr,
              min_line_len=50,
              canny1=80, canny2=160,
              approx_eps=0.015,           # relative to contour perimeter
              close_radius=8,            # morphology closing radius (px)
              min_poly_area=800,         # min filled area for polygon (px^2)
              merge_angle=8.0,
              merge_dist=10.0):
    h, w = img_bgr.shape[:2]
    gray0 = to_gray(img_bgr)

    # deskew to align near 0/90
    gray, _ = auto_deskew(gray0, canny1, canny2)

    # robust binarisation (combine adaptive + Otsu)
    blur = cv.GaussianBlur(gray, (5,5), 0)
    th_ad = cv.adaptiveThreshold(blur, 255, cv.ADAPTIVE_THRESH_GAUSSIAN_C,
                                 cv.THRESH_BINARY_INV, 31, 5)
    _, th_otsu = cv.threshold(blur, 0, 255, cv.THRESH_BINARY_INV+cv.THRESH_OTSU)
    mask = cv.max(th_ad, th_otsu)

    # remove tiny blobs (likely text), keep elongated bits
    mask = remove_small_blobs(mask, min_area=min_poly_area//2, max_aspect_keep_long=7.0)

    # close small gaps so contours become watertight
    if close_radius > 0:
        mask = close_gaps(mask, close_radius)

    items = []
    black = color_hex(0xff,0,0,0)
    transparent = color_hex(0x00,0,0,0)

    # --- Lines: Canny + HoughP + merging ---
    edges = cv.Canny(gray, canny1, canny2)
    raw = cv.HoughLinesP(edges, 1, np.pi/180, threshold=70,
                         minLineLength=min_line_len, maxLineGap=8)
    if raw is not None:
        merged = merge_collinear_segments(raw[:,0,:], angle_tol_deg=merge_angle,
                                          dist_tol=merge_dist, min_len=min_line_len)
        for x1,y1,x2,y2 in merged:
            items.append(make_line(x1,y1,x2,y2, layer=0, color=black, width=1.0))

    # --- Polygons: external contours on cleaned mask ---
    contours, hierarchy = cv.findContours(mask, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)
    for c in contours:
        area = cv.contourArea(c)
        if area < min_poly_area:
            continue
        peri = cv.arcLength(c, True)
        eps = max(1.5, approx_eps * peri)
        approx = cv.approxPolyDP(c, eps, True)
        pts = [(int(p[0][0]), int(p[0][1])) for p in approx]
        if len(pts) >= 3:
            items.append(make_polygon(pts, layer=0, stroke=black, width=1.0,
                                      fill=transparent, fillStyle=0))

    return {"items": items}

# ---------- API ----------
@app.post("/vectorise")
async def vectorise_endpoint(
    image: UploadFile = File(...),

    # tuning knobs (defaults are sensible)
    min_line_len: int   = Form(40),
    canny1: int         = Form(80),
    canny2: int         = Form(160),
    approx_eps: float   = Form(0.01),   # fraction of perimeter
    close_radius: int   = Form(3),      # px
    min_poly_area: int  = Form(250),    # px^2
    merge_angle: float  = Form(6.0),    # degrees
    merge_dist: float   = Form(6.0),    # px
):
    raw = await image.read()
    file_bytes = np.frombuffer(raw, np.uint8)
    img = cv.imdecode(file_bytes, cv.IMREAD_UNCHANGED)
    if img is None:
        return JSONResponse({"error":"Could not decode image"}, status_code=400)

    payload = vectorise(img,
                        min_line_len=min_line_len,
                        canny1=canny1, canny2=canny2,
                        approx_eps=approx_eps,
                        close_radius=close_radius,
                        min_poly_area=min_poly_area,
                        merge_angle=merge_angle,
                        merge_dist=merge_dist)
    return JSONResponse(payload)

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)

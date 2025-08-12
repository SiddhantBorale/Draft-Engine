# server.py
# pip install fastapi uvicorn opencv-python-headless numpy python-multipart
from fastapi import FastAPI, File, UploadFile, Form
from fastapi.responses import JSONResponse
import uvicorn
import numpy as np
import cv2 as cv
import math

app = FastAPI()

# ---------- small utils ----------
def color_hex(a, r, g, b): return f"#{a:02x}{r:02x}{g:02x}{b:02x}"

def make_line(x1,y1,x2,y2, layer=0, color="#ff000000", width=1.0):
    return {"type":"line","x1":float(x1),"y1":float(y1),"x2":float(x2),"y2":float(y2),
            "color":color,"width":float(width),"layer":int(layer)}

def make_polygon(points, layer=0, stroke="#ff000000", width=1.0, fill="#00000000", fillStyle=0):
    return {"type":"polygon","points":[{"x":float(x),"y":float(y)} for (x,y) in points],
            "color":stroke,"width":float(width),"fill":fill,"fillStyle":int(fillStyle),"layer":int(layer)}

# ---------- geometry helpers ----------
def dist2(a,b):
    dx, dy = (a[0]-b[0]), (a[1]-b[1]); return dx*dx + dy*dy

def project_on_segment(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    vx, vy = (bx-ax), (by-ay); vv = vx*vx + vy*vy
    if vv <= 1e-9: return a, 0.0
    t = ((px-ax)*vx + (py-ay)*vy) / vv
    t = max(0.0, min(1.0, t))
    return (ax + t*vx, ay + t*vy), t

def seg_endpoint_proj_dist2(P, A, B):
    Q, _ = project_on_segment(P, A, B)
    return dist2(P, Q)

def line_angle_deg_xy(x1,y1,x2,y2):
    return (math.degrees(math.atan2(y2-y1, x2-x1)) + 180.0) % 180.0

def near_line_duplicate(a, b, tol_px=6.0, ang_deg=8.0):
    ax1,ay1,ax2,ay2 = a; bx1,by1,bx2,by2 = b
    a_ang = line_angle_deg_xy(ax1,ay1,ax2,ay2)
    b_ang = line_angle_deg_xy(bx1,by1,bx2,by2)
    d_ang = min(abs(a_ang-b_ang), 180-abs(a_ang-b_ang))
    if d_ang > ang_deg: return False
    # endpoints of each must lie close to the other segment
    A1 = (ax1,ay1); A2=(ax2,ay2); B1=(bx1,by1); B2=(bx2,by2)
    tol2 = tol_px*tol_px
    a_close = min(seg_endpoint_proj_dist2(A1,B1,B2), seg_endpoint_proj_dist2(A2,B1,B2)) <= tol2
    b_close = min(seg_endpoint_proj_dist2(B1,A1,A2), seg_endpoint_proj_dist2(B2,A1,A2)) <= tol2
    return a_close and b_close

# ---------- preprocessing ----------
def protect_lines_mask(gray, canny1=80, canny2=160, min_len=60, gap=8, thickness=4):
    edges = cv.Canny(gray, canny1, canny2)
    lines = cv.HoughLinesP(edges, 1, np.pi/180, threshold=80,
                           minLineLength=min_len, maxLineGap=gap)
    mask = np.zeros_like(gray, dtype=np.uint8)
    if lines is not None:
        for x1, y1, x2, y2 in lines[:, 0, :]:
            cv.line(mask, (x1, y1), (x2, y2), 255, thickness)
    return cv.dilate(mask, cv.getStructuringElement(cv.MORPH_RECT, (3, 3)), 1)

def suppress_text(gray, bin_inv, gentle=False):
    # gentle=True makes it less aggressive (to keep thin strokes)
    line_protect = protect_lines_mask(gray, canny1=80, canny2=160, min_len=60, gap=8,
                                      thickness=3 if gentle else 5)

    k = 3 if gentle else 3
    er1  = cv.erode(bin_inv, cv.getStructuringElement(cv.MORPH_RECT, (k, k)), 1)
    thin = cv.subtract(bin_inv, er1)

    bh_k = 11 if gentle else 15
    bh   = cv.morphologyEx(gray, cv.MORPH_BLACKHAT, cv.getStructuringElement(cv.MORPH_RECT, (bh_k, bh_k)))
    _, bh_th = cv.threshold(bh, 0, 255, cv.THRESH_BINARY + cv.THRESH_OTSU)

    cand = cv.bitwise_and(thin, bh_th)
    cand = cv.bitwise_and(cand, cv.bitwise_not(line_protect))

    erase = np.zeros_like(bin_inv, dtype=np.uint8)
    contours, _ = cv.findContours(cand, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)
    for c in contours:
        x, y, w, h = cv.boundingRect(c)
        area = cv.contourArea(c); boxA = w*h
        if w < 6 or h < 6: continue
        if boxA > (12000 if gentle else 20000): continue
        ar = w / float(h)
        fill_ratio = area / float(boxA + 1e-6)
        if 0.25 < ar < 4.5 and 0.08 < fill_ratio < (0.6 if gentle else 0.65):
            if cv.countNonZero(line_protect[y:y+h, x:x+w]) > 0: continue
            cv.rectangle(erase, (x-1, y-1), (x+w+1, y+h+1), 255, -1)

    return cv.bitwise_and(bin_inv, cv.bitwise_not(erase))

def side_denoise(bin_inv, gentle=False):
    if gentle:
        # very light cleanup to preserve hairlines
        k3  = cv.getStructuringElement(cv.MORPH_RECT, (3,3))
        x = cv.morphologyEx(bin_inv, cv.MORPH_CLOSE, k3, iterations=1)
        return x
    # stronger cleanup (default)
    k3  = cv.getStructuringElement(cv.MORPH_RECT, (3,3))
    k5h = cv.getStructuringElement(cv.MORPH_RECT, (5,1))
    k5v = cv.getStructuringElement(cv.MORPH_RECT, (1,5))
    x = cv.morphologyEx(bin_inv, cv.MORPH_CLOSE, k3, iterations=1)
    x = cv.morphologyEx(x, cv.MORPH_OPEN,  k3, iterations=1)
    h = cv.morphologyEx(x, cv.MORPH_OPEN, k5h, iterations=1)
    v = cv.morphologyEx(x, cv.MORPH_OPEN, k5v, iterations=1)
    x = cv.max(x, cv.max(h, v))
    x = cv.medianBlur(x, 3)
    return x

def remove_small_curvy_doors(bin_inv):
    contours, _ = cv.findContours(bin_inv, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)
    keep = bin_inv.copy()
    door_boxes = []
    for c in contours:
        area = cv.contourArea(c)
        if area < 60 or area > 15000: continue
        peri = cv.arcLength(c, True)
        if peri <= 1e-6: continue
        circ = (4.0*np.pi*area) / (peri*peri)
        x,y,w,h = cv.boundingRect(c)
        ar = w/float(h) if h>0 else 0
        if 0.55 <= circ <= 0.9 and 0.6 <= ar <= 1.7:
            cv.drawContours(keep, [c], -1, 0, thickness=-1)
            door_boxes.append((x-4, y-4, w+8, h+8))
    return keep, door_boxes

# ---------- refine / snap ----------
def snap_axis(p, q, axis_snap_deg):
    ang = (math.degrees(math.atan2(q[1]-p[1], q[0]-p[0])) + 180.0) % 180.0
    dev0 = abs(ang - 0.0); dev90 = abs(ang - 90.0)
    if min(dev0, dev90) <= axis_snap_deg:
        if dev90 < dev0:
            x = 0.5*(p[0]+q[0]); return (x, p[1]), (x, q[1])
        else:
            y = 0.5*(p[1]+q[1]); return (p[0], y), (q[0], y)
    return p, q

def refine_lines(lines, axis_snap_deg=6.0, merge_px=6.0, extend_px=10.0, min_len_px=12.0):
    L = []
    for (x1,y1,x2,y2) in lines:
        p1, p2 = snap_axis((x1,y1), (x2,y2), axis_snap_deg)
        L.append([p1[0],p1[1],p2[0],p2[1]])
    L = [l for l in L if dist2((l[0],l[1]),(l[2],l[3])) >= (min_len_px*min_len_px)]

    merge2 = merge_px*merge_px
    changed = True
    while changed:
        changed = False
        out = []; used = [False]*len(L)
        for i in range(len(L)):
            if used[i]: continue
            xi,yi,xj,yj = L[i]
            pi=(xi,yi); pj=(xj,yj)
            merged = False
            for k in range(i+1,len(L)):
                if used[k]: continue
                xa,ya,xb,yb = L[k]
                pa=(xa,ya); pb=(xb,yb)
                share = (dist2(pi,pa)<=merge2 or dist2(pi,pb)<=merge2 or
                         dist2(pj,pa)<=merge2 or dist2(pj,pb)<=merge2)
                if not share: continue
                ai = math.atan2(yj-yi, xj-xi); ak = math.atan2(yb-ya, xb-xa)
                d  = abs(ai - ak); d = min(d, math.pi - d)
                if d <= math.radians(axis_snap_deg):
                    pts = np.array([pi,pj,pa,pb], dtype=np.float32)
                    horiz = abs(yj-yi) < abs(xj-xi)
                    order = np.argsort(pts[:,0] if horiz else pts[:,1])
                    pmin = tuple(pts[order[0]]); pmax = tuple(pts[order[-1]])
                    out.append([pmin[0],pmin[1],pmax[0],pmax[1]])
                    used[i]=used[k]=True; merged = changed = True; break
            if not merged and not used[i]:
                out.append(L[i]); used[i]=True
        L = out

    extend2 = extend_px*extend_px
    for idx in range(len(L)):
        x1,y1,x2,y2 = L[idx]
        for j in range(len(L)):
            if j==idx: continue
            xa,ya,xb,yb = L[j]
            for end in [0,1]:
                P = (x1,y1) if end==0 else (x2,y2)
                Q, t = project_on_segment(P, (xa,ya), (xb,yb))
                if 0.0 < t < 1.0 and dist2(P,Q) <= extend2:
                    if end==0: L[idx][0],L[idx][1] = Q
                    else:      L[idx][2],L[idx][3] = Q
    return L

def add_room_closures(lines, door_boxes, close_px=8.0, axis_snap_deg=6.0, min_len_px=12.0):
    ends = []
    for (x1,y1,x2,y2) in lines:
        ends.append((x1,y1)); ends.append((x2,y2))
    close2 = close_px*close_px
    added = []
    for i in range(len(ends)):
        for j in range(i+1,len(ends)):
            if dist2(ends[i], ends[j]) <= close2:
                p,q = snap_axis(ends[i], ends[j], axis_snap_deg)
                if dist2(p,q) < (min_len_px*min_len_px): continue
                bx1=min(p[0],q[0]); by1=min(p[1],q[1]); bx2=max(p[0],q[0]); by2=max(p[1],q[1])
                skip=False
                for (x,y,w,h) in door_boxes:
                    if (bx2>=x and bx1<=x+w and by2>=y and by1<=y+h): skip=True; break
                if not skip: added.append([p[0],p[1],q[0],q[1]])
    return added

# ---------- pipeline ----------
def vectorise(
    img_bgr,
    min_line_len: int = 40,
    canny1: int = 80,
    canny2: int = 160,
    approx_eps: float = 2.0,
    do_text_suppr: bool = True,
    do_side_denoise: bool = True,
    do_door_simpl: bool = True,
    do_room_close: bool = True,
    detail_mode: bool = True,   # NEW: keep thin lines
):
    gray = cv.cvtColor(img_bgr, cv.COLOR_BGR2GRAY)

    # gentler smoothing in detail mode
    if detail_mode:
        gray = cv.bilateralFilter(gray, d=5, sigmaColor=30, sigmaSpace=5)
    else:
        gray = cv.bilateralFilter(gray, d=7, sigmaColor=40, sigmaSpace=7)

    # binary (foreground=white=255)
    bin_inv = cv.adaptiveThreshold(
        gray, 255, cv.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv.THRESH_BINARY_INV, 25, 5
    )

    # text + denoise tuned by detail_mode
    if do_text_suppr:
        bin_inv = suppress_text(gray, bin_inv, gentle=detail_mode)
    if do_side_denoise:
        bin_inv = side_denoise(bin_inv, gentle=detail_mode)

    door_boxes = []
    if do_door_simpl:
        bin_inv, door_boxes = remove_small_curvy_doors(bin_inv)

    # --- TWO-PASS LINE DETECTION ---
    raw_lines = []

    # 1) Coarse (robust walls) on binarized edges
    edges_coarse = cv.Canny(bin_inv, max(30, canny1), max(60, canny2))
    lines_coarse = cv.HoughLinesP(edges_coarse, 1, np.pi/180, threshold=60,
                                  minLineLength=min_line_len, maxLineGap=8)
    if lines_coarse is not None:
        for x1,y1,x2,y2 in lines_coarse[:,0,:]:
            raw_lines.append((int(x1),int(y1),int(x2),int(y2)))

    # 2) Fine (thin strokes) directly on gray with lower thresholds
    if detail_mode:
        t1 = max(10, int(canny1*0.5))
        t2 = max(20, int(canny2*0.7))
        edges_thin = cv.Canny(gray, t1, t2)
        lines_thin = cv.HoughLinesP(edges_thin, 1, np.pi/180, threshold=30,
                                    minLineLength=max(10, int(min_line_len*0.5)),
                                    maxLineGap=8)
        if lines_thin is not None:
            for x1,y1,x2,y2 in lines_thin[:,0,:]:
                cand = (int(x1),int(y1),int(x2),int(y2))
                # de-dup vs what we already have
                dup = False
                for base in raw_lines:
                    if near_line_duplicate(base, cand, tol_px=6.0, ang_deg=8.0):
                        dup = True; break
                if not dup:
                    raw_lines.append(cand)

    # refine & optionally close rooms
    refined = refine_lines(raw_lines, axis_snap_deg=6.0, merge_px=6.0, extend_px=10.0, min_len_px=12.0)
    if do_room_close:
        refined += add_room_closures(refined, door_boxes, close_px=8.0, axis_snap_deg=6.0, min_len_px=12.0)

    # pack items
    items = []
    black = color_hex(0xff,0,0,0)
    transparent = color_hex(0x00,0,0,0)
    for (x1,y1,x2,y2) in refined:
        items.append(make_line(x1,y1,x2,y2, layer=0, color=black, width=1.0))

    # polygons from bin_inv (unchanged)
    contours, _ = cv.findContours(bin_inv, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)
    for c in contours:
        area = cv.contourArea(c)
        if area < 120: continue
        eps = max(approx_eps, 0.01*cv.arcLength(c, True))
        approx = cv.approxPolyDP(c, eps, True)
        pts = [(int(p[0][0]), int(p[0][1])) for p in approx]
        if len(pts) >= 3:
            items.append(make_polygon(pts, layer=0, stroke=black, width=1.0, fill=transparent, fillStyle=0))
    return {"items": items}

# ---------- API ----------
@app.post("/vectorise")
async def vectorise_endpoint(
    image: UploadFile = File(...),
    min_line_len: int = Form(25),
    canny1: int = Form(50),
    canny2: int = Form(120),
    approx_eps: float = Form(2.0),
    text_suppr: int = Form(10),
    side_denoise_on: int = Form(1),
    door_simpl: int = Form(10),
    room_close: int = Form(1),
    detail_mode: int = Form(1),  # NEW: keep thin lines
):
    raw = await image.read()
    file_bytes = np.frombuffer(raw, np.uint8)
    img = cv.imdecode(file_bytes, cv.IMREAD_COLOR)
    if img is None:
        return JSONResponse({"error":"Could not decode image"}, status_code=400)

    payload = vectorise(
        img,
        min_line_len=min_line_len,
        canny1=canny1, canny2=canny2,
        approx_eps=approx_eps,
        do_text_suppr=bool(text_suppr),
        do_side_denoise=bool(side_denoise_on),
        do_door_simpl=bool(door_simpl),
        do_room_close=bool(room_close),
        detail_mode=bool(detail_mode),
    )
    return JSONResponse(payload)

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)

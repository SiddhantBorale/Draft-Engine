# server.py
# pip install fastapi uvicorn opencv-python-headless numpy python-multipart
from fastapi import FastAPI, File, UploadFile, Form
from fastapi.responses import JSONResponse
import uvicorn
import numpy as np
import cv2 as cv

app = FastAPI()

# ------------------------ small helpers ------------------------
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
        "type": "polygon",
        "points": [{"x": float(x), "y": float(y)} for (x,y) in points],
        "color": stroke, "width": float(width),
        "fill": fill, "fillStyle": int(fillStyle),
        "layer": int(layer)
    }

def dist2(a,b):
    dx, dy = (a[0]-b[0]), (a[1]-b[1])
    return dx*dx + dy*dy

# ------------------------ pre/post passes ------------------------
def protect_lines_mask(gray, canny1=80, canny2=160, min_len=60, gap=8, thickness=5):
    edges = cv.Canny(gray, canny1, canny2)
    lines = cv.HoughLinesP(edges, 1, np.pi/180, threshold=80,
                           minLineLength=min_len, maxLineGap=gap)
    mask = np.zeros_like(gray, dtype=np.uint8)
    if lines is not None:
        for x1, y1, x2, y2 in lines[:, 0, :]:
            cv.line(mask, (x1, y1), (x2, y2), 255, thickness)
    return cv.dilate(mask, cv.getStructuringElement(cv.MORPH_RECT, (3, 3)), 1)

def suppress_text_safe(gray, bin_inv):
    # Protect strong straight lines (walls) before erasing text
    line_protect = protect_lines_mask(gray, canny1=80, canny2=160, min_len=60, gap=8, thickness=5)
    # “Thin strokes” via erode diff
    er1  = cv.erode(bin_inv, cv.getStructuringElement(cv.MORPH_RECT, (3, 3)), 1)
    thin = cv.subtract(bin_inv, er1)
    # Black-hat to find dark small text on bright bg
    bh   = cv.morphologyEx(gray, cv.MORPH_BLACKHAT, cv.getStructuringElement(cv.MORPH_RECT, (15, 15)))
    _, bh_th = cv.threshold(bh, 0, 255, cv.THRESH_BINARY + cv.THRESH_OTSU)
    cand = cv.bitwise_and(thin, bh_th)
    cand = cv.bitwise_and(cand, cv.bitwise_not(line_protect))
    erase = np.zeros_like(bin_inv, dtype=np.uint8)
    contours, _ = cv.findContours(cand, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)
    for c in contours:
        x, y, w, h = cv.boundingRect(c)
        area = cv.contourArea(c)
        if w < 6 or h < 6:           # too tiny
            continue
        if w*h > 20000:              # too big to be text
            continue
        ar = w / float(h)
        fill_ratio = area / float(w*h + 1e-6)
        if 0.25 < ar < 4.5 and 0.08 < fill_ratio < 0.65:
            roiP = line_protect[y:y+h, x:x+w]
            if cv.countNonZero(roiP) > 0:
                continue
            cv.rectangle(erase, (x-1, y-1), (x+w+1, y+h+1), 255, -1)
    return cv.bitwise_and(bin_inv, cv.bitwise_not(erase))

def side_denoise(bin_inv):
    k3 = cv.getStructuringElement(cv.MORPH_RECT, (3,3))
    k5h = cv.getStructuringElement(cv.MORPH_RECT, (5,1))
    k5v = cv.getStructuringElement(cv.MORPH_RECT, (1,5))
    x = cv.morphologyEx(bin_inv, cv.MORPH_CLOSE, k3, iterations=1)
    x = cv.morphologyEx(x, cv.MORPH_OPEN,  k3, iterations=1)
    # keep strokes that support H/V structure
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
        if area < 60 or area > 15000:
            continue
        peri = cv.arcLength(c, True)
        if peri <= 1e-6:
            continue
        circ = (4.0*np.pi*area) / (peri*peri)
        x,y,w,h = cv.boundingRect(c)
        ar = w/float(h) if h>0 else 0
        if 0.55 <= circ <= 0.9 and 0.6 <= ar <= 1.7:
            cv.drawContours(keep, [c], -1, 0, thickness=-1)
            door_boxes.append((x-4, y-4, w+8, h+8))
    return keep, door_boxes

# ------------------------ line detectors ------------------------
def detect_lines_lsd(gray, min_len_px=12, refine=cv.LSD_REFINE_ADV):
    """
    Detect thin/short segments with LSD (no ML, but great recall).
    Returns list of (x1,y1,x2,y2).
    """
    # Some builds expose createLineSegmentDetector; if not, fall back to FastLineDetector if available.
    lines_out = []
    try:
        lsd = cv.createLineSegmentDetector(_refine=refine)
        res = lsd.detect(gray)
        if res is None:
            return lines_out
        lines = res[0]  # Nx1x4
        if lines is None:
            return lines_out
        for l in lines.reshape(-1,4):
            x1,y1,x2,y2 = map(float, l)
            if (x1-x2)**2 + (y1-y2)**2 >= (min_len_px*min_len_px):
                lines_out.append((x1,y1,x2,y2))
    except Exception:
        # Optional: FastLineDetector (requires ximgproc; skip if not present)
        try:
            fld = cv.ximgproc.createFastLineDetector(length_threshold=min_len_px)
            lines = fld.detect(gray)
            if lines is not None:
                for l in lines.reshape(-1,4):
                    x1,y1,x2,y2 = map(float, l)
                    lines_out.append((x1,y1,x2,y2))
        except Exception:
            pass
    return lines_out

def detect_lines_hough(gray, min_line_len=40, canny1=80, canny2=160, max_gap=6):
    edges = cv.Canny(gray, canny1, canny2)
    linesP = cv.HoughLinesP(edges, 1, np.pi/180, threshold=60,
                            minLineLength=min_line_len, maxLineGap=max_gap)
    out = []
    if linesP is not None:
        for x1,y1,x2,y2 in linesP[:,0,:]:
            out.append((float(x1),float(y1),float(x2),float(y2)))
    return out

# ------------------------ vectorise core ------------------------
def vectorise(
    img_bgr,
    # detection
    min_line_len: int = 40,
    canny1: int = 80,
    canny2: int = 160,
    approx_eps: float = 2.0,
    # toggles
    do_text_suppr: bool = True,
    do_side_denoise: bool = True,
    do_door_simpl: bool = True,
    do_room_close: bool = True,
    # thin lines
    use_lsd: bool = True,
    lsd_min_len: int = 12
):
    h, w = img_bgr.shape[:2]
    gray = cv.cvtColor(img_bgr, cv.COLOR_BGR2GRAY)

    # Gentle denoise to keep hairlines
    gray = cv.bilateralFilter(gray, d=5, sigmaColor=30, sigmaSpace=7)

    # Binary (foreground=255)
    bin_inv = cv.adaptiveThreshold(
        gray, 255, cv.ADAPTIVE_THRESH_GAUSSIAN_C, cv.THRESH_BINARY_INV, 25, 5
    )

    if do_text_suppr:
        bin_inv = suppress_text_safe(gray, bin_inv)

    if do_side_denoise:
        bin_inv = side_denoise(bin_inv)

    door_boxes = []
    if do_door_simpl:
        bin_inv, door_boxes = remove_small_curvy_doors(bin_inv)

    items = []
    black = color_hex(0xff,0,0,0)
    transparent = color_hex(0x00,0,0,0)

    # --- Thin lines (LSD) ---
    if use_lsd:
        lsd_lines = detect_lines_lsd(gray, min_len_px=lsd_min_len, refine=cv.LSD_REFINE_ADV)
        for x1,y1,x2,y2 in lsd_lines:
            items.append(make_line(x1,y1,x2,y2, layer=1, color=black, width=0.8))

    # --- (Optional) Hough lines for sturdier walls ---
    hough_lines = detect_lines_hough(gray, min_line_len=min_line_len, canny1=canny1, canny2=canny2, max_gap=6)
    for x1,y1,x2,y2 in hough_lines:
        items.append(make_line(x1,y1,x2,y2, layer=0, color=black, width=1.2))

    # --- Contours → polygons (walls/rooms mass) ---
    contours, _ = cv.findContours(bin_inv, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)
    for c in contours:
        area = cv.contourArea(c)
        if area < 120:
            continue
        eps = max(approx_eps, 0.01*cv.arcLength(c, True))
        approx = cv.approxPolyDP(c, eps, True)
        pts = [(p[0][0], p[0][1]) for p in approx]
        if len(pts) >= 3:
            items.append(make_polygon(pts, layer=2, stroke=black, width=1.0,
                                      fill=transparent, fillStyle=0))

    # (Optional) simple room closure can be added client-side with your C++ refine pass

    return {"items": items}

# ------------------------ API ------------------------
@app.post("/vectorise")
async def vectorise_endpoint(
    image: UploadFile = File(...),
    min_line_len: int = Form(40),
    canny1: int = Form(80),
    canny2: int = Form(160),
    approx_eps: float = Form(2.0),
    # toggles
    text_suppr: int = Form(1),
    side_denoise_on: int = Form(1),
    door_simpl: int = Form(1),
    room_close: int = Form(1),   # (kept for compatibility; closure is client-side refine)
    # thin lines
    use_lsd: int = Form(1),
    lsd_min_len: int = Form(12),
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
        use_lsd=bool(use_lsd),
        lsd_min_len=lsd_min_len
    )
    return JSONResponse(payload)

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)

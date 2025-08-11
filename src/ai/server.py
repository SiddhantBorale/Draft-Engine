# server.py
# pip install fastapi uvicorn opencv-python-headless numpy
from fastapi import FastAPI, File, UploadFile, Form
from fastapi.responses import JSONResponse
import uvicorn
import numpy as np
import cv2 as cv
import io

app = FastAPI()

def color_hex(a, r, g, b):
    # ARGB like Qt::name(QColor::HexArgb)
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

def vectorise(img_bgr, min_line_len=40, canny1=80, canny2=160, approx_eps=2.0):
    h, w = img_bgr.shape[:2]
    gray = cv.cvtColor(img_bgr, cv.COLOR_BGR2GRAY)
    # light denoise
    gray = cv.GaussianBlur(gray, (5,5), 0)
    # adaptive threshold → crisp edges
    th = cv.adaptiveThreshold(gray, 255, cv.ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv.THRESH_BINARY_INV, 25, 5)

    # --- lines via HoughP ---
    edges = cv.Canny(gray, canny1, canny2)
    linesP = cv.HoughLinesP(edges, 1, np.pi/180, threshold=60,
                            minLineLength=min_line_len, maxLineGap=6)

    items = []
    black = color_hex(0xff,0,0,0)
    transparent = color_hex(0x00,0,0,0)

    if linesP is not None:
        for l in linesP[:,0,:]:
            x1,y1,x2,y2 = map(int, l)
            items.append(make_line(x1,y1,x2,y2, layer=0, color=black, width=1.0))

    # --- contours → polygons (walls, shapes) ---
    contours, _ = cv.findContours(th, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)
    for c in contours:
        area = cv.contourArea(c)
        if area < 100:   # ignore tiny specks
            continue
        # simplify
        eps = max(approx_eps, 0.01*cv.arcLength(c, True))
        approx = cv.approxPolyDP(c, eps, True)
        pts = [(p[0][0], p[0][1]) for p in approx]
        if len(pts) >= 3:
            items.append(make_polygon(pts, layer=0, stroke=black, width=1.0,
                                      fill=transparent, fillStyle=0))

    return {"items": items}

@app.post("/vectorise")
async def vectorise_endpoint(
    image: UploadFile = File(...),
    min_line_len: int = Form(40),
    canny1: int = Form(80),
    canny2: int = Form(160),
    approx_eps: float = Form(2.0),
):
    raw = await image.read()
    file_bytes = np.frombuffer(raw, np.uint8)
    img = cv.imdecode(file_bytes, cv.IMREAD_COLOR)
    if img is None:
        return JSONResponse({"error":"Could not decode image"}, status_code=400)

    payload = vectorise(img, min_line_len, canny1, canny2, approx_eps)
    return JSONResponse(payload)

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)

# src/ai/vectorizer.py
from dataclasses import dataclass, asdict
from typing import List, Dict, Any, Tuple
import numpy as np
import cv2 as cv

# ---------------------- helpers ----------------------
def _deskew(gray: np.ndarray) -> np.ndarray:
    # estimate skew from dominant line angle
    edges = cv.Canny(gray, 80, 160)
    lines = cv.HoughLines(edges, 1, np.pi/180, 150)
    if lines is None:
        return gray
    angles = []
    for rho, theta in lines[:,0]:
        # map angle to [-90,90)
        ang = (theta * 180/np.pi) - 90
        if -85 < ang < 85:
            angles.append(ang)
    if not angles:
        return gray
    angle = np.median(angles)
    (h, w) = gray.shape[:2]
    M = cv.getRotationMatrix2D((w/2, h/2), angle, 1.0)
    return cv.warpAffine(gray, M, (w, h), flags=cv.INTER_LINEAR, borderMode=cv.BORDER_REPLICATE)

def _adaptive_bin(gray: np.ndarray) -> np.ndarray:
    gray = cv.bilateralFilter(gray, 7, 50, 50)
    clahe = cv.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
    gray = clahe.apply(gray)
    bw = cv.adaptiveThreshold(gray, 255, cv.ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv.THRESH_BINARY, 31, 5)
    bw = 255 - bw  # lines as white
    return bw

def _thin(bw: np.ndarray) -> np.ndarray:
    # needs contrib
    return cv.ximgproc.thinning(bw, thinningType=cv.ximgproc.THINNING_ZHANGSUEN)

def _mask_text(gray: np.ndarray) -> np.ndarray:
    # quick & dirty text suppression via MSER boxes
    mser = cv.MSER_create(_delta=5, _min_area=60, _max_area=8000)
    regions, _ = mser.detectRegions(gray)
    mask = np.ones_like(gray, dtype=np.uint8) * 255
    for pts in regions:
        x, y, w, h = cv.boundingRect(pts.reshape(-1,1,2))
        if h < 5 or w < 5: continue
        cv.rectangle(mask, (x,y), (x+w, y+h), 0, -1)
    return mask

def _merge_collinear(lines: List[Tuple[float,float,float,float]], ang_tol_deg=2.0, dist_tol=3.0):
    # simple O(n^2) merge for short lists
    def angle(x1,y1,x2,y2):
        return np.degrees(np.arctan2(y2-y1, x2-x1)) % 180
    merged = []
    used = [False]*len(lines)
    for i,a in enumerate(lines):
        if used[i]: continue
        x1,y1,x2,y2 = a
        A = np.array([[x1,y1],[x2,y2]], dtype=float)
        ai = angle(*a)
        for j,b in enumerate(lines):
            if i==j or used[j]: continue
            bj = angle(*b)
            if abs((ai - bj + 90) % 180 - 90) > ang_tol_deg:  # angular diff
                continue
            # project endpoints, see if they are close to same infinite line
            # quick check via distance between endpoints
            x3,y3,x4,y4 = b
            if min(
                np.hypot(x1-x3, y1-y3), np.hypot(x1-x4, y1-y4),
                np.hypot(x2-x3, y2-y3), np.hypot(x2-x4, y2-y4)
            ) <= dist_tol:
                A = np.vstack([A, [[x3,y3],[x4,y4]]])
                used[j] = True
        # extend to min/max along the dominant axis
        vx = np.cos(np.radians(ai)); vy = np.sin(np.radians(ai))
        t = A @ np.array([vx, vy])
        i_min, i_max = np.argmin(t), np.argmax(t)
        p1 = A[i_min]; p2 = A[i_max]
        merged.append((float(p1[0]), float(p1[1]), float(p2[0]), float(p2[1])))
    return merged

def _fit_circles(contour: np.ndarray, min_arc_deg=20) -> List[Tuple[float,float,float,float,float]]:
    # split contour into chunks and try circle fit; return arcs as (cx,cy,r, start_deg, end_deg)
    arcs = []
    pts = contour.reshape(-1,2).astype(np.float32)
    n = len(pts)
    if n < 8:
        return arcs
    step = max(8, n//12)
    i = 0
    while i+step < n:
        chunk = pts[i:i+step]
        x = chunk[:,0]; y = chunk[:,1]
        # algebraic circle fit (Taubin)
        x_m = x.mean(); y_m = y.mean()
        u = x - x_m; v = y - y_m
        Suu = (u*u).sum(); Svv = (v*v).sum()
        Suv = (u*v).sum(); Suuu = (u*u*u).sum(); Svvv = (v*v*v).sum()
        Suvv = (u*v*v).sum(); Svuu = (v*u*u).sum()
        A = np.array([[Suu, Suv],[Suv, Svv]])
        b = 0.5*np.array([Suuu + Suvv, Svvv + Svuu])
        try:
            uc, vc = np.linalg.solve(A, b)
        except np.linalg.LinAlgError:
            i += step//2
            continue
        cx = x_m + uc; cy = y_m + vc
        r = float(np.mean(np.hypot(x - cx, y - cy)))
        # compute angular extent
        ang = np.degrees(np.arctan2(y - cy, x - cx))
        a0, a1 = float(ang[0]), float(ang[-1])
        extent = abs(((a1 - a0 + 180) % 360) - 180)
        if extent >= min_arc_deg and 5 < r < 1e5:
            arcs.append((float(cx), float(cy), float(r), a0, a1))
        i += step//2
    return arcs

# ---------------------- main entry ----------------------
def vectorize_blueprint(bgr: np.ndarray, cfg: Dict[str, Any]) -> Dict[str, Any]:
    dpi = int(cfg.get("dpi", 300))
    mask_text = bool(cfg.get("mask_text", True))
    min_len_px = float(cfg.get("min_line_len_px", 12))
    line_merge_ang = float(cfg.get("merge_angle_deg", 2.0))
    line_merge_dist = float(cfg.get("merge_dist_px", 3.0))

    gray = cv.cvtColor(bgr, cv.COLOR_BGR2GRAY)
    gray = _deskew(gray)

    if mask_text:
        tx = _mask_text(gray)
        gray = cv.bitwise_and(gray, tx)

    bw = _adaptive_bin(gray)
    thin = _thin(bw)

    # --------- Lines (FastLineDetector) ----------
    fld = cv.ximgproc.createFastLineDetector(
        length_threshold=int(min_len_px),
        distance_threshold=1.414,
        canny_th1=40, canny_th2=160, canny_aperture_size=3,
        do_merge=False
    )
    raw = fld.detect(thin)  # ndarray of N x 4 (x1,y1,x2,y2)
    raw_lines = []
    if raw is not None:
        raw_lines = [(float(x1),float(y1),float(x2),float(y2)) for x1,y1,x2,y2 in raw.reshape(-1,4)]

    lines = _merge_collinear(raw_lines, line_merge_ang, line_merge_dist)

    # --------- Arcs/Circles ----------
    contours, _ = cv.findContours((thin>0).astype(np.uint8), cv.RETR_LIST, cv.CHAIN_APPROX_NONE)
    arcs = []
    polys = []
    for c in contours:
        if len(c) < 20: continue
        fitted = _fit_circles(c)
        arcs.extend(fitted)

        # polygonal regions (fill), simplify with approxPolyDP
        epsilon = 1.5
        approx = cv.approxPolyDP(c, epsilon, closed=True).reshape(-1,2)
        if len(approx) >= 3:
            polys.append([(float(p[0]), float(p[1])) for p in approx])

    # --------- Package result ----------
    return {
        "ok": True,
        "dpi": dpi,
        "lines": [{"x1":x1,"y1":y1,"x2":x2,"y2":y2} for (x1,y1,x2,y2) in lines],
        "arcs":  [{"cx":cx,"cy":cy,"r":r,"a0":a0,"a1":a1} for (cx,cy,r,a0,a1) in arcs],
        "polys": [{"points": pts} for pts in polys]
    }

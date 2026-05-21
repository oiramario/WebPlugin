import struct

def load(path):
    with open(path, "rb") as f:
        d = f.read()
    off = struct.unpack_from("<I", d, 10)[0]
    w = struct.unpack_from("<i", d, 18)[0]
    h = abs(struct.unpack_from("<i", d, 22)[0])
    stride = ((w * 24 + 31) // 32) * 4
    px = []
    for y in range(h-1, -1, -1):
        row = d[off + y*stride : off + y*stride + w*3]
        for x in range(w):
            px.append((row[x*3+2], row[x*3+1], row[x*3]))
    return w, h, px

def analyze(path_a, path_b, label):
    w, h, a = load(path_a)
    _, _, b = load(path_b)
    total = w * h
    diff_pixels = 0
    sum_diff = 0
    max_diff = 0
    for i in range(total):
        d = max(abs(a[i][0]-b[i][0]), abs(a[i][1]-b[i][1]), abs(a[i][2]-b[i][2]))
        if d > 0:
            diff_pixels += 1
            sum_diff += d
            max_diff = max(max_diff, d)
    print(f"{label}: {diff_pixels}/{total} pixels differ ({100*diff_pixels/total:.2f}%)")
    if diff_pixels:
        print(f"  max diff per channel = {max_diff}  avg diff = {sum_diff/diff_pixels:.2f}")

analyze(r"D:\1_filter_0.bmp", r"D:\1_no_filter_0.bmp", "1.webp")
analyze(r"D:\2_filter_0.bmp", r"D:\2_no_filter_0.bmp", "2.webp")

import struct, sys, os

def read_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    bf_offbits = struct.unpack_from('<I', data, 10)[0]
    bi_width = struct.unpack_from('<i', data, 18)[0]
    bi_height = struct.unpack_from('<i', data, 22)[0]
    abs_h = abs(bi_height)
    row_size = ((bi_width * 24 + 31) // 32) * 4
    pixels = []
    for y in range(abs_h - 1, -1, -1):
        row_start = bf_offbits + y * row_size
        row = data[row_start:row_start + bi_width * 3]
        for x in range(bi_width):
            b = row[x * 3]
            g = row[x * 3 + 1]
            r = row[x * 3 + 2]
            pixels.append((r, g, b))
    return bi_width, abs_h, pixels

def write_diff_bmp(width, height, pixels_a, pixels_b, out_path):
    row_size = ((width * 3 + 3) // 4) * 4
    pad = b'\x00' * (row_size - width * 3)
    image_size = row_size * height
    file_size = 14 + 40 + image_size
    with open(out_path, 'wb') as f:
        f.write(b'BM')
        f.write(struct.pack('<I', file_size))
        f.write(struct.pack('<HH', 0, 0))
        f.write(struct.pack('<I', 14 + 40))
        f.write(struct.pack('<I', 40))
        f.write(struct.pack('<i', width))
        f.write(struct.pack('<i', height))
        f.write(struct.pack('<H', 1))
        f.write(struct.pack('<H', 24))
        f.write(struct.pack('<I', 0))
        f.write(struct.pack('<I', image_size))
        f.write(struct.pack('<i', 2835))
        f.write(struct.pack('<i', 2835))
        f.write(struct.pack('<I', 0))
        f.write(struct.pack('<I', 0))
        for y in range(height - 1, -1, -1):
            for x in range(width):
                i = y * width + x
                diff = max(
                    abs(pixels_a[i][0] - pixels_b[i][0]),
                    abs(pixels_a[i][1] - pixels_b[i][1]),
                    abs(pixels_a[i][2] - pixels_b[i][2])
                )
                if diff == 0:
                    f.write(struct.pack('BBB', 60, 60, 60))
                else:
                    intensity = min(255, diff * 6)
                    f.write(struct.pack('BBB', 0, 0, intensity))
            f.write(pad)

def main():
    if len(sys.argv) < 3:
        print("Usage: python bmp_diff.py <file_a.bmp> <file_b.bmp>")
        return
    path_a, path_b = sys.argv[1], sys.argv[2]
    w_a, h_a, px_a = read_bmp(path_a)
    w_b, h_b, px_b = read_bmp(path_b)
    if w_a != w_b or h_a != h_b:
        print(f"Dimension mismatch: {w_a}x{h_a} vs {w_b}x{h_b}")
        return
    base_a = os.path.basename(path_a)
    parts = base_a.split('_')
    label = parts[0]
    out = f"D:\\{label}_diff_0.bmp"
    write_diff_bmp(w_a, h_a, px_a, px_b, out)
    print(f"Diff saved to {out}")

if __name__ == '__main__':
    main()

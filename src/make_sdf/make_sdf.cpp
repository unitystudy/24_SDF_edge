// usage:
//
// make_sdf filename -o dest.png -s sdf.png -h 120
//  filename: 入力色ファイル名
//  -o:  出力ファイル名（色）
//  -s:  出力ファイル名（SDF）
//  -h:  出力縦幅。ひとまず縦横元の解像度の1/2のべき乗を期待
//  -t:  あるなしを判断するアルファ値の閾値

#include "pch.h"
#include <iostream>

// png読み書きライブラリ
#define STB_IMAGE_IMPLEMENTATION
#include "../library/nothings_stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../library/nothings_stb/stb_image_write.h"

struct INIT_DATA{
	int out_height;// 出力縦幅。ひとまず縦横元の解像度の1/2のべき乗を期待
	const char *filename;// 入力ファイル名
	const char *out_sdf;// 出力SDFファイル名
	const char *out_rgba;// 出力色ファイル名
	unsigned char threshhold;// あるなしを判断するアルファ値の閾値
};

struct RGBA8 {
	unsigned char r, g, b, a;
};
struct RGBA32F {
	float r, g, b, a;
};

// l以上で最も小さい2のべき乗の数
unsigned int get_level_of_power_of_2(unsigned int l)
{
	unsigned int po = 0;
	while ( (1u << po) < l ) po++;
	return po;
}

float col2float(unsigned char c) {
	return (1.0f/255.0f)*(float)c;
}
unsigned char float2col(float c) {
	return (unsigned char)(255.0 * c + 0.5f / 255.0f);
}

void reduction(
	const unsigned char *src, unsigned int src_w, unsigned int src_h, 
	unsigned char *dest, unsigned int dest_w, unsigned int dest_h)
{
	RGBA8 *d = (RGBA8*)dest;

	// box filtering
	for (unsigned int y = 0; y < dest_h; y++) {
		unsigned int sy_min = y * src_h / dest_h;
		unsigned int sy_max = (y + 1) * src_h / dest_h;
		const unsigned char *spy = src + 4 * sy_min * src_w;
		for (unsigned int x = 0; x < dest_w; x++) {
			unsigned int sx_min = x * src_w / dest_w;
			unsigned int sx_max = (x + 1) * src_w / dest_w;
			const unsigned char *spx = spy + 4 * sx_min;

			RGBA32F col = { 0.0f, 0.0f, 0.0f, 0.0f };
			const RGBA8 *p = (const RGBA8 *)spx;
			for (unsigned int yy = sy_min; yy < sy_max; yy++) {
				for (unsigned int xx = sx_min; xx < sx_max; xx++) {
					float a = col2float(p->a);
					col.a += a;
					col.r += col2float(p->r) * a;
					col.g += col2float(p->g) * a;
					col.b += col2float(p->b) * a;
					p++;
				}
				p += src_w - (sx_max - sx_min);
			}

			// 重み補正
			assert(sy_min != sy_max);
			assert(sx_min != sx_max);
			float w_inv = 1.0f / (float)((sx_max - sx_min)*(sy_max - sy_min));
			col.r *= w_inv;
			col.g *= w_inv;
			col.b *= w_inv;
			col.a *= w_inv;

			// 事前乗算アルファ補正
			if (0.001 < col.a) {
				col.r /= col.a;
				col.g /= col.a;
				col.b /= col.a;
			}

			// 出力
			d->r = float2col(col.r);
			d->g = float2col(col.g);
			d->b = float2col(col.b);
			d->a = float2col(col.a);
			d++;
		}
	}
}

// 2D空間のモートン番号を算出
unsigned int get_morton_index(unsigned int x, unsigned int y)
{
	auto  bit_separate = [](unsigned int n){
		n = (n | (n << 8)) & 0x00ff00ff;
		n = (n | (n << 4)) & 0x0f0f0f0f;
		n = (n | (n << 2)) & 0x33333333;
		n = (n | (n << 1)) & 0x55555555;
		return n;
	};
	return (bit_separate(x) | (bit_separate(y) << 1));
}

void morton_order(unsigned char *dest, const RGBA8 *src, unsigned int w, unsigned int h, unsigned int size)
{
	for (unsigned int y = 0; y < h; y++) {
		for (unsigned int x = 0; x < w; x++) {
			dest[get_morton_index(x, y)] = src[x + y * w].a;
		}
		// 右側あまり
		for (unsigned int x = w; x < size; x++) {
			dest[get_morton_index(x, y)] = 0;
		}
	}
	// 下側あまり
	for (unsigned int y = h; y < size; y++) {
		for (unsigned int x = 0; x < size; x++) {
			dest[get_morton_index(x, y)] = 0;
		}
	}
}
// 最大値での hierarchy 計算
void morton_order_hierarchy(unsigned char *dest, const unsigned char *src, unsigned int size)
{
	for (unsigned int idx = 0; idx < size * size; idx++) {
		const unsigned char *p = src + (idx << 2);
		unsigned char a0 = p[0];
		unsigned char a1 = p[1];
		unsigned char a2 = p[2];
		unsigned char a3 = p[3];
		unsigned char b0 = (a0 < a1) ? a1 : a0;
		unsigned char b1 = (a2 < a3) ? a3 : a2;
		dest[idx] = (b0 < b1) ? b1 : b0;
		p += 4;
	}
}

#define MAX(a,b) (((a)<(b))?(b):(a))
#define MIN(a,b) (((a)<(b))?(a):(b))

struct bbox{
	unsigned int min[2];
	unsigned int max[2];
};

unsigned int get_max_distance(const bbox b, unsigned int x, unsigned int y)
{
	unsigned int dx0 = (b.max[0] - x)*(b.max[0] - x);
	unsigned int dx1 = (b.min[0] - x)*(b.min[0] - x);
	unsigned int dy0 = (b.max[1] - y)*(b.max[1] - y);
	unsigned int dy1 = (b.min[1] - y)*(b.min[1] - y);

	return ((dx0 < dx1) ? dx1 : dx0) + ((dy0 < dy1) ? dy1 : dy0);
}

unsigned int get_distance(unsigned int idx, unsigned int level, const bbox &bb, unsigned int src_x, unsigned int src_y, unsigned char **a_morton_ordered_a, unsigned char threshhold)
{
	// データのない領域
	const unsigned char *morton_ordered_a = a_morton_ordered_a[level];
	if (morton_ordered_a[idx] < threshhold) return 0xffffffff;

	// そこにデータはある
	if (level == 0) return 0;

	// 何かあるので、子供をさらに探索
	unsigned int distance = 0xffffffff;
	idx *= 4;
	unsigned int center[2] = {(bb.min[0]+bb.max[0])/2, (bb.min[0]+bb.max[1])/ 2 };
	bbox b0 = { {bb.min[0], bb.min[1]},{center[0], center[1]} };
	if (get_max_distance(b0, src_x, src_y) < distance) 
	{
		unsigned int d = get_distance(idx + 0, level - 1, b0, src_x, src_y, a_morton_ordered_a, threshhold);
		distance = MIN(distance, d);
	}
	bbox b1 = { {center[0], bb.min[1]},{bb.max[0], center[1]} };
	if (get_max_distance(b1, src_x, src_y) < distance) 
	{
		unsigned int d = get_distance(idx + 1, level - 1, b1, src_x, src_y, a_morton_ordered_a, threshhold);
		distance = MIN(distance, d);
	}
	bbox b2 = { {bb.min[0], center[1]},{center[0], bb.max[1]} };
	if (get_max_distance(b2, src_x, src_y) < distance) 
	{
		unsigned int d = get_distance(idx + 2, level - 1, b2, src_x, src_y, a_morton_ordered_a, threshhold);
		distance = MIN(distance, d);
	}
	bbox b3 = { {center[0], center[1]},{bb.max[0], bb.max[1]} };
	if (get_max_distance(b3, src_x, src_y) < distance) 
	{
		unsigned int d = get_distance(idx + 3, level - 1, b3, src_x, src_y, a_morton_ordered_a, threshhold);
		distance = MIN(distance, d);
	}

	return distance;
}

void generate_sdf(RGBA8 *dest, unsigned int dw, unsigned int dh, unsigned char **a_morton_ordered_a, unsigned int max_level, unsigned int sw, unsigned int sh, unsigned char threshhold)
{
	for (unsigned int y = 0; y < dh; y++) {
		for (unsigned int x = 0; x < dw; x++) {
			unsigned int src_x = (x * sw + (sw >> 1)) / dw;// ピクセル中心の元の画像の位置
			unsigned int src_y = (y * sh + (sh >> 1)) / dh;

			// (src_x, src_y から最も近い位置を見つけていく)
			unsigned int size = 1u << max_level;
			bbox bb = { {0, 0}, {size, size} };
			float distance = sqrt((float)get_distance(0, max_level, bb, src_x, src_y, a_morton_ordered_a, threshhold));

			RGBA8 &c = dest[y * dw + x];
			c.a = (unsigned char)(MAX(0.0f, MIN(255.999999f, distance + 128.0f)));// -128から127で0.5オフセットで正値化
			c.r = (unsigned char)(MAX(0.0f, MIN(255.999999f, distance)));// 正の値で記録
			c.g = (unsigned char)(MAX(0.0f, MIN(255.999999f,-distance)));// 負の値で記録
			c.b = (0 <= distance) ? 0 : 255;// 正負
		}
	}
}

void make_sdf(const INIT_DATA &init)
{
	// load data
	int w;
	int h;
	int channels;// 3 or 4
	unsigned char* image = stbi_load(init.filename, &w, &h, &channels, STBI_rgb_alpha);
	if (image == nullptr) throw(std::string("Failed to load texture"));
	if (channels != 4) throw(std::string("No alpha image"));// need RGBA channel

	// create reduction color image
	int sw = init.out_height * w / h;
	int sh = init.out_height;
	RGBA8 *img_rgba = new RGBA8[sw * sh];
	reduction(image, w, h, (unsigned char*)img_rgba, sw, sh);

	// save reduction color image
	int stride_in_bytes = sw * 4;
	stbi_write_png(init.out_rgba, sw, sh, STBI_rgb_alpha, (unsigned char*)img_rgba, stride_in_bytes);
	delete[] img_rgba;

	// create Morton order alpha image
	unsigned int lx = get_level_of_power_of_2(w);
	unsigned int ly = get_level_of_power_of_2(h);
	unsigned int level = (lx < ly) ? ly : lx;// max
	unsigned int size2 = 1u << level;// w,h以上で最小の2のべき乗のサイズ
	unsigned char **a_morton_ordered_a = new unsigned char *[level+1];

	a_morton_ordered_a[0] = new unsigned char[size2 * size2];
	morton_order(a_morton_ordered_a[0], (const RGBA8 *)image, w, h, size2);

	// create Morton order alpha image hierarchy
	for (unsigned int i = 1; i <= level; i++) {
		size2 >>= 1;
		a_morton_ordered_a[i] = new unsigned char[size2 * size2];
		morton_order_hierarchy(a_morton_ordered_a[i], a_morton_ordered_a[i-1], size2);
	}

	// create SDF
	RGBA8 *img_sdf = new RGBA8[sw * sh];
	generate_sdf(img_sdf, sw, sh, a_morton_ordered_a, level, w, h, init.threshhold);
	stbi_write_png(init.out_sdf, sw, sh, STBI_rgb_alpha, (unsigned char*)img_sdf, stride_in_bytes);

	// release
	delete[] img_sdf;
	for (unsigned int i = 0; i <= level; i++) {
		delete[] a_morton_ordered_a[i];
	}
	delete[] a_morton_ordered_a;
	stbi_image_free(image);
}

int main(int argc, char *argv[])
{
	INIT_DATA src;

	src.out_height = 64;
	src.filename = "src.png";
	src.out_sdf  = "sdf.png";
	src.out_rgba = "dest.png";
	src.threshhold = 255;

	for (int i = 1; i < argc; i++) {
		switch (argv[i][0]) {
		case '-':
			switch (argv[i][1]) {
			case 'o':
				src.out_rgba = argv[++i];
				break;
			case 's':
				src.out_sdf = argv[++i];
				break;
			case 'h':
				src.out_height = atoi(argv[++i]);
				break;
			case 't':
				src.threshhold = atoi(argv[++i]);
				break;
			default:
				break;
			}
			break;
		default:
			src.filename = argv[i];
			break;
		}
	}

	make_sdf(src);

    return 0; 
}

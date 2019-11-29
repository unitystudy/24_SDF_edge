// usage:
//
// make_sdf filename -o dest.png -s sdf.png -h 120
//  filename: 入力色ファイル名
//  -o:  出力ファイル名（色: ボックスフィルタリング）
//  -s:  出力ファイル名（SDF R:正規化 G:絶対値 B:符号 A:255）
//  -h:  出力高さ。ひとまず縦横元の解像度の1/整数を期待
//  -t:  あるなしを判断するアルファ値の閾値(1-255)



#include "pch.h"
#include <iostream>

// png読み書きライブラリ
#define STB_IMAGE_IMPLEMENTATION
#include "../library/nothings_stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../library/nothings_stb/stb_image_write.h"

// 独自min/max
#define MAX(a,b) (((a)<(b))?(b):(a))
#define MIN(a,b) (((a)<(b))?(a):(b))

// アプリケーションの初期化データ(引数から構築)
struct INIT_DATA{
	int out_height;				// 出力高さ。ひとまず縦横元の解像度の1/整数を期待
	const char *filename;		// 入力ファイル名
	const char *out_sdf;		// 出力SDFファイル名
	const char *out_rgba;		// 出力色ファイル名
	unsigned char threshhold;	// あるなしを判断するアルファ値の閾値(1-255)
};

// 2次元浮動小数点数ベクトル
struct vec2 {
	float x, y;
};

// 2次元の境界箱
struct bbox {
	unsigned int min[2];
	unsigned int max[2];

	// 境界箱への最短距離(の2乗)
	float min_distance_sq(const vec2& pos) const {
		float dx = 0.0f;
		float dy = 0.0f;
		if (pos.x < min[0]) dx = min[0] - pos.x;
		if (pos.y < min[1]) dy = min[1] - pos.y;
		if (max[0] < pos.x) dx = pos.x - max[0];
		if (max[1] < pos.y) dy = pos.y - max[1];
		return dx * dx + dy * dy;
	}
};

// 色構造体
struct RGBA8 {
	unsigned char r, g, b, a;
};
struct RGBA32F {
	float r, g, b, a;
};

// 色の型変換
float col2float(unsigned char c) {
	return (1.0f / 255.0f) * (float)c;
}
unsigned char float2col(float c) {
	return (unsigned char)(255.0 * c + 0.5f / 255.0f);
}

// l以上で最も小さい2のべき乗の数
unsigned int get_level_of_power_of_2(unsigned int l)
{
	unsigned int po = 0;
	while ( (1u << po) < l ) po++;
	return po;
}

// 縮小画像の作成
void reduction(
	const unsigned char *src, unsigned int src_w, unsigned int src_h, 
	unsigned char *dest, unsigned int dest_w, unsigned int dest_h)
{
	// box filtering
#ifdef _OPENMP
	#pragma omp parallel for
#endif
	for (int y = 0; y < (int)dest_h; y++) {
		RGBA8* d = (RGBA8*)dest + y * dest_w;
		unsigned int sy_min = y * src_h / dest_h;
		unsigned int sy_max = (y + 1) * src_h / dest_h;
		const unsigned char *spy = src + 4u * sy_min * src_w;
		for (unsigned int x = 0; x < dest_w; x++) {
			unsigned int sx_min = x * src_w / dest_w;
			unsigned int sx_max = (x + 1) * src_w / dest_w;
			const unsigned char *spx = spy + 4u * sx_min;

			RGBA32F col = { 0.0f, 0.0f, 0.0f, 0.0f };
			const RGBA8 *p = (const RGBA8 *)spx;
			for (unsigned int yy = sy_min; yy < sy_max; yy++) {
				for (unsigned int xx = sx_min; xx < sx_max; xx++) {
					float a = col2float(p->a);
					float r = col2float(p->r);
					float g = col2float(p->g);
					float b = col2float(p->b);

					// 線形空間へガンマ補正
					r = powf(r, 2.2f);
					g = powf(g, 2.2f);
					b = powf(b, 2.2f);

					col.a += a;
					col.r += r * a;
					col.g += g * a;
					col.b += b * a;
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

			// 逆ガンマ補正
			col.r = powf(col.r, 1.0f / 2.2f);
			col.g = powf(col.g, 1.0f / 2.2f);
			col.b = powf(col.b, 1.0f / 2.2f);

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

// 画像データのアルファ成分からモートン順列のマスクを作成
void morton_order(unsigned char *dest, const RGBA8 *src, 
	unsigned int w, unsigned int h, unsigned int size, unsigned char threshhold)
{
#ifdef _OPENMP
	#pragma omp parallel for
#endif
	for (int y = 0; y < (int)h; y++) {
		for (unsigned int x = 0; x < w; x++) {
			dest[get_morton_index(x, y)] = (threshhold <= src[x + y * w].a) ? 255 : 0;
		}
		// 右側あまり
		for (unsigned int x = w; x < size; x++) {
			dest[get_morton_index(x, y)] = 0;
		}
	}

#ifdef _OPENMP
	#pragma omp parallel for
#endif
	// 下側あまり
	for (int y = h; y < (int)size; y++) {
		for (unsigned int x = 0; x < size; x++) {
			dest[get_morton_index(x, y)] = 0;
		}
	}
}

// 最大値での hierarchy 計算
void morton_order_hierarchy_max(unsigned char *dest, const unsigned char *src, unsigned int size)
{
#ifdef _OPENMP
	#pragma omp parallel for
#endif
	for (int idx = 0; idx < (int)(size * size); idx++) {
		const unsigned char *p = src + (idx << 2u);
		dest[idx] = MAX(MAX(p[0], p[1]), MAX(p[2], p[3]));
		p += 4;
	}
}

// 最小値での hierarchy 計算
void morton_order_hierarchy_min(unsigned char* dest, const unsigned char* src, unsigned int size)
{
#ifdef _OPENMP
	#pragma omp parallel for
#endif
	for (int idx = 0; idx < (int)(size * size); idx++) {
		const unsigned char* p = src + (idx << 2u);
		dest[idx] = MIN(MIN(p[0], p[1]), MIN(p[2], p[3]));
		p += 4;
	}
}


float get_outer_distance_sq(unsigned int idx, unsigned int level, const bbox &bb, const vec2 &pos
	, const unsigned char** a_morton_ordered_a_min, const unsigned char** a_morton_ordered_a_max)
{
	// 完全に埋まっていれば一番近い場所を返す
	if (127 < a_morton_ordered_a_min[level][idx]) return bb.min_distance_sq(pos);

	// 完全に空いていればいれば無効な値（最遠方）を返す
	if (a_morton_ordered_a_max[level][idx] <= 127) return FLT_MAX;

	// 最下層では上記のどちらかにひっかかるはず
	assert(level);

	// 何かあるので、4分木で子供をさらに探索
	level--;
	idx *= 4;
	float distance = FLT_MAX;
	unsigned int center[2] = {(bb.min[0]+bb.max[0]) >> 1, (bb.min[1]+bb.max[1]) >> 1 };
	bbox b0 = { {bb.min[0], bb.min[1]},{center[0], center[1]} };
	if (b0.min_distance_sq(pos) < distance)// カリング
	{
		distance = get_outer_distance_sq(idx + 0, level, b0, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		if (distance < FLT_MIN) return 0.0f;
	}
	bbox b1 = { {center[0], bb.min[1]},{bb.max[0], center[1]} };
	if (b1.min_distance_sq(pos) < distance)
	{
		float d = get_outer_distance_sq(idx + 1, level, b1, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		distance = MIN(distance, d);
		if (distance < FLT_MIN) return 0.0f;
	}
	bbox b2 = { {bb.min[0], center[1]},{center[0], bb.max[1]} };
	if (b2.min_distance_sq(pos) < distance)
	{
		float d = get_outer_distance_sq(idx + 2, level, b2, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		distance = MIN(distance, d);
		if (distance < FLT_MIN) return 0.0f;
	}
	bbox b3 = { {center[0], center[1]},{bb.max[0], bb.max[1]} };
	if (b3.min_distance_sq(pos) < distance)
	{
		float d = get_outer_distance_sq(idx + 3, level, b3, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		distance = MIN(distance, d);
//		if (distance < FLT_MIN) return 0.0f;// 結局返すので要らない
	}

	return distance;
}

float get_inner_distance_sq(unsigned int idx, unsigned int level, const bbox& bb, const vec2 &pos
	, const unsigned char** a_morton_ordered_a_min, const unsigned char** a_morton_ordered_a_max)
{
	// 完全に空いていればいれば一番近い場所を返す
	if (a_morton_ordered_a_max[level][idx] <= 127) return bb.min_distance_sq(pos);

	// 完全に埋まっていれば無効な値（最遠方）を返す
	if (127 < a_morton_ordered_a_min[level][idx]) return FLT_MAX;

	// 最下層では上記のどちらかにひっかかるはず
	assert(level);

	// 何かあるので、4分木で子供をさらに探索
	float distance = FLT_MAX;
	level--;
	idx *= 4;
	unsigned int center[2] = { (bb.min[0] + bb.max[0]) / 2, (bb.min[1] + bb.max[1]) / 2 };
	bbox b0 = { {bb.min[0], bb.min[1]},{center[0], center[1]} };
	if (b0.min_distance_sq(pos) < distance)// カリング
	{
		distance = get_inner_distance_sq(idx + 0, level, b0, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		if (distance < FLT_MIN) return 0;
	}
	bbox b1 = { {center[0], bb.min[1]},{bb.max[0], center[1]} };
	if (b1.min_distance_sq(pos) < distance)
	{
		float d = get_inner_distance_sq(idx + 1, level, b1, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		distance = MIN(distance, d);
		if (distance < FLT_MIN) return 0;
	}
	bbox b2 = { {bb.min[0], center[1]},{center[0], bb.max[1]} };
	if (b2.min_distance_sq(pos) < distance)
	{
		float d = get_inner_distance_sq(idx + 2, level, b2, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		distance = MIN(distance, d);
		if (distance < FLT_MIN) return 0;
	}
	bbox b3 = { {center[0], center[1]},{bb.max[0], bb.max[1]} };
	if (b3.min_distance_sq(pos) < distance)
	{
		float d = get_inner_distance_sq(idx + 3, level, b3, pos, a_morton_ordered_a_min, a_morton_ordered_a_max);
		distance = MIN(distance, d);
	}

	return distance;
}

// SDFの作成
void generate_sdf(RGBA8 *dest, unsigned int dw, unsigned int dh
	, const unsigned char** a_morton_ordered_a_max, const unsigned char** a_morton_ordered_a_min
	, unsigned int max_level, unsigned int sw, unsigned int sh)
{
#ifdef _OPENMP
	#pragma omp parallel for
#endif
	for (int y = 0; y < (int)dh; y++) {
		for (unsigned int x = 0; x < dw; x++) {
			// ピクセル中心の元の画像の位置
			vec2 src_pos = { (float)((x * sw + (sw >> 1)) / dw) , (float)((y * sh + (sh >> 1)) / dh) };

			// (src_x, src_y から最も近い位置を見つけていく)
			unsigned int size = 1u << max_level;
			bbox bb = { {0, 0}, {size, size} };
			float distance = sqrt(get_outer_distance_sq(0, max_level, bb, src_pos, a_morton_ordered_a_min, a_morton_ordered_a_max));
			if (distance < FLT_MIN) {
				distance = -sqrt(get_inner_distance_sq(0, max_level, bb, src_pos, a_morton_ordered_a_min, a_morton_ordered_a_max));
			}

			RGBA8 &c = dest[y * dw + x];
			c.a = 255;
			c.r = (unsigned char)(MAX(0.0f, MIN(255.99999f, distance + 128.0f)));// -128から127で0.5オフセットで正値化
			c.g = (unsigned char)(MAX(0.0f, MIN(255.99999f, (0.0f < distance) ? distance : -distance)));// 絶対値で記録
			c.b = (0.0f <= distance) ? 0 : 255;// 符号
		}
	}
}

void make_sdf(const INIT_DATA &init)
{
	// load data
	int w, h;
	int channels;// 3 or 4
	unsigned char* image = stbi_load(init.filename, &w, &h, &channels, STBI_rgb_alpha);
	if (image == nullptr) throw(std::string("Failed to load texture"));
	if (channels != 4) throw(std::string("No alpha image"));// need RGBA channel

	// generate reduction color image
	int sw = init.out_height * w / h;
	int sh = init.out_height;
	RGBA8 *img_rgba = new RGBA8[sw * sh];
	reduction(image, w, h, (unsigned char*)img_rgba, sw, sh);

	// save reduction color image
	int stride_in_bytes = sw * 4;
	stbi_write_png(init.out_rgba, sw, sh, STBI_rgb_alpha, (unsigned char*)img_rgba, stride_in_bytes);
	delete[] img_rgba;

	// create Morton order alpha mask
	unsigned int lx = get_level_of_power_of_2(w);
	unsigned int ly = get_level_of_power_of_2(h);
	unsigned int level = MAX(lx, ly);
	unsigned int size2 = 1u << level;// w,h以上で最小の2のべき乗のサイズ
	unsigned char** a_morton_ordered_a_max = new unsigned char* [level + 1u];
	unsigned char** a_morton_ordered_a_min = new unsigned char* [level + 1u];

	a_morton_ordered_a_max[0] = new unsigned char[size2 * size2];
	a_morton_ordered_a_min[0] = a_morton_ordered_a_max[0];
	morton_order(a_morton_ordered_a_max[0], (const RGBA8 *)image, w, h, size2, init.threshhold);

	// create Morton order alpha image hierarchy
	for (unsigned int i = 1; i <= level; i++) {
		size2 >>= 1;
		a_morton_ordered_a_max[i] = new unsigned char[size2 * size2];
		a_morton_ordered_a_min[i] = new unsigned char[size2 * size2];
		morton_order_hierarchy_max(a_morton_ordered_a_max[i], a_morton_ordered_a_max[i - 1], size2);
		morton_order_hierarchy_min(a_morton_ordered_a_min[i], a_morton_ordered_a_min[i - 1], size2);
	}

	// create SDF
	RGBA8 *img_sdf = new RGBA8[sw * sh];
	generate_sdf(img_sdf, sw, sh
		, (const unsigned char**)a_morton_ordered_a_max, (const unsigned char**)a_morton_ordered_a_min
		, level, w, h);
	stbi_write_png(init.out_sdf, sw, sh, STBI_rgb_alpha, (unsigned char*)img_sdf, stride_in_bytes);
	delete[] img_sdf;

	// release memories
	for (unsigned int i = 1; i <= level; i++) {
		delete[] a_morton_ordered_a_max[i];
		delete[] a_morton_ordered_a_min[i];
	}
	delete[] a_morton_ordered_a_max[0];// 0成分はmin/max共通
	delete[] a_morton_ordered_a_max;
	delete[] a_morton_ordered_a_min;
	stbi_image_free(image);
}

int main(int argc, char *argv[])
{
	// 引数のディフォルト値を設定
	INIT_DATA src;
	src.out_height = 64;
	src.filename = "src.png";
	src.out_sdf  = "sdf.png";
	src.out_rgba = "dest.png";
	src.threshhold = 1;

	// 引数の解析
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

	// 実行
	make_sdf(src);

    return 0; 
}

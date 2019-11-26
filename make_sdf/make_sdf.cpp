// usage:
//
// make_sdf filename -o dest.png -s sdf.png -h 120
//  filename: 入力色ファイル名
//  -o:  出力ファイル名（色）
//  -s:  出力ファイル名（SDF）
//  -h:  縮小率を指定。ひとまず元の解像度の1/2のべき乗を期待

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
};

struct RGBA8{
	unsigned char r, g, b, a;
};

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
	RGBA8 *p = (RGBA8*)dest;

	for (unsigned int y = 0; y < dest_h; y++) {
		unsigned int sy = y * src_h / dest_h;
		const unsigned char *spy = src + 4 * sy * src_w;
		for (unsigned int x = 0; x < dest_w; x++) {
			// point sampling
			unsigned int sx = x * src_w / dest_w;
			const unsigned char *sp = spy + 4 * sx;
			p->r = sp[0];
			p->g = sp[1];
			p->b = sp[2];
			p->a = sp[3];
			p++;
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

	// create z-order memory image

	// generate mips

	// 縮小イメージの作成
	int sw = init.out_height * w / h;
	int sh = init.out_height;
	RGBA8 *img_rgba = new RGBA8[sw * sh];
	RGBA8 *img_sdf = new RGBA8[sw * sh];


	// create SDF

	// create reduction color image
	reduction(image, w, h, (unsigned char*)img_rgba, sw, sh);

	// save data
	int stride_in_bytes = sw * 4;
	stbi_write_png(init.out_rgba, sw, sh, STBI_rgb_alpha, (unsigned char*)img_rgba, stride_in_bytes);
	stbi_write_png(init.out_sdf, sw, sh, STBI_rgb_alpha, (unsigned char*)img_sdf, stride_in_bytes);

	// 解法
	delete[] img_rgba;
	delete[] img_sdf;
	stbi_image_free(image);
}

int main(int argc, char *argv[])
{
	INIT_DATA src;

	src.out_height = 64;
	src.filename = "src.png";
	src.out_sdf  = "sdf.png";
	src.out_rgba = "dest.png";

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

// プログラムの実行: Ctrl + F5 または [デバッグ] > [デバッグなしで開始] メニュー
// プログラムのデバッグ: F5 または [デバッグ] > [デバッグの開始] メニュー

// 作業を開始するためのヒント: 
//    1. ソリューション エクスプローラー ウィンドウを使用してファイルを追加/管理します 
//   2. チーム エクスプローラー ウィンドウを使用してソース管理に接続します
//   3. 出力ウィンドウを使用して、ビルド出力とその他のメッセージを表示します
//   4. エラー一覧ウィンドウを使用してエラーを表示します
//   5. [プロジェクト] > [新しい項目の追加] と移動して新しいコード ファイルを作成するか、[プロジェクト] > [既存の項目の追加] と移動して既存のコード ファイルをプロジェクトに追加します
//   6. 後ほどこのプロジェクトを再び開く場合、[ファイル] > [開く] > [プロジェクト] と移動して .sln ファイルを選択します

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <stack>

#include <omp.h>

const double PI = 3.14159265358979323846;
const double INF = 1e20;
const double EPS = 1e-6;
const double MaxDepth = 5;

// *** その他の関数 ***
inline double clamp(double x){ return x<0 ? 0 : x>1 ? 1 : x; } 
inline int toInt(double x){ return int(pow(clamp(x),1/2.2)*255+.5); } 
inline double rand01() { return (double)rand()/RAND_MAX; }

// *** データ構造 ***
struct Vec {
	double x, y, z;
	Vec(const double x_ = 0, const double y_ = 0, const double z_ = 0) : x(x_), y(y_), z(z_) {}
	inline Vec operator+(const Vec &b) const {return Vec(x + b.x, y + b.y, z + b.z);}
	inline Vec operator-(const Vec &b) const {return Vec(x - b.x, y - b.y, z - b.z);}
	inline Vec operator*(const double b) const {return Vec(x * b, y * b, z * b);}
	inline Vec operator/(const double b) const {return Vec(x / b, y / b, z / b);}
	inline const double LengthSquared() const { return x*x + y*y + z*z; }
	inline const double Length() const { return sqrt(LengthSquared()); }
};
inline Vec operator*(double f, const Vec &v) { return v * f; }
inline Vec Normalize(const Vec &v) { return v / v.Length(); }
// 要素ごとの積をとる
inline const Vec Multiply(const Vec &v1, const Vec &v2) {
	return Vec(v1.x * v2.x, v1.y * v2.y, v1.z * v2.z);
}
inline const double Dot(const Vec &v1, const Vec &v2) {
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}
inline const Vec Cross(const Vec &v1, const Vec &v2) {
	return Vec((v1.y * v2.z) - (v1.z * v2.y), (v1.z * v2.x) - (v1.x * v2.z), (v1.x * v2.y) - (v1.y * v2.x));
}
typedef Vec Color;
const Color BackgroundColor(0.0, 0.0, 0.0);

struct Ray {
	Vec org, dir;
	Ray(const Vec org_, const Vec &dir_) : org(org_), dir(dir_) {}
};

enum ReflectionType {
	DIFFUSE,    // 完全拡散面。いわゆるLambertian面。
	SPECULAR,   // 理想的な鏡面。
	REFRACTION, // 理想的なガラス的物質。
};

struct Sphere {
	double radius;
	Vec position;
	Color emission, color;
	ReflectionType ref_type;

	Sphere(const double radius_, const Vec &position_, const Color &emission_, const Color &color_, const ReflectionType ref_type_) :
	  radius(radius_), position(position_), emission(emission_), color(color_), ref_type(ref_type_) {}
	// 入力のrayに対する交差点までの距離を返す。交差しなかったら0を返す。
	const double intersect(const Ray &ray) {
		Vec o_p = position - ray.org;
		const double b = Dot(o_p, ray.dir), det = b * b - Dot(o_p, o_p) + radius * radius;
		if (det >= 0.0) {
			const double sqrt_det = sqrt(det);
			const double t1 = b - sqrt_det, t2 = b + sqrt_det;
			if (t1 > EPS)		return t1;
			else if(t2 > EPS)	return t2;
		}
		return 0.0;
	}
};

// *** レンダリングするシーンデータ ****
// from small ppt
Sphere spheres[] = {
	/*
	Sphere(5.0, Vec(50.0, 75.0, 81.6),Color(12,12,12), Color(), DIFFUSE),//照明
	Sphere(1e5, Vec(50, 1e5, 81.6),    Color(), Color(0.75, 0.75, 0.75),DIFFUSE),// 床
	Sphere(16.5,Vec(73,16.5,78),       Color(), Color(1,1,1)*.99, REFRACTION),//ガラス
	*/
	Sphere(5.0, Vec(50.0, 75.0, 81.6),Color(12,12,12), Color(), DIFFUSE),//照明
	Sphere(1e5, Vec( 1e5+1,40.8,81.6), Color(), Color(0.75, 0.25, 0.25),DIFFUSE),// 左
	Sphere(1e5, Vec(-1e5+99,40.8,81.6),Color(), Color(0.25, 0.25, 0.75),DIFFUSE),// 右
	Sphere(1e5, Vec(50,40.8, 1e5),     Color(), Color(0.75, 0.75, 0.75),DIFFUSE),// 奥
	Sphere(1e5, Vec(50,40.8,-1e5+170), Color(), Color(), DIFFUSE),// 手前
	Sphere(1e5, Vec(50, 1e5, 81.6),    Color(), Color(0.75, 0.75, 0.75),DIFFUSE),// 床
	Sphere(1e5, Vec(50,-1e5+81.6,81.6),Color(), Color(0.75, 0.75, 0.75),DIFFUSE),// 天井
	Sphere(16.5,Vec(27,16.5,47),       Color(), Color(1,1,1)*.99, SPECULAR),// 鏡
	Sphere(16.5,Vec(73,16.5,78),       Color(), Color(1,1,1)*.99, REFRACTION),//ガラス
};
const int LightID = 0;

// *** レンダリング用関数 ***
// シーンとの交差判定関数
inline bool intersect_scene(const Ray &ray, double *t, int *id) {
	const double n = sizeof(spheres) / sizeof(Sphere);
	*t  = INF;
	*id = -1;
	for (int i = 0; i < int(n); i ++) {
		double d = spheres[i].intersect(ray);
		if (d > 0.0 && d < *t) {
			*t  = d;
			*id = i;
		}
	}
	return *t < INF;
}

// 乱数は基本的にこのSamplerから得る
// 乱数空間で変異することでパスを変異させる
// 本当はもうちょっと賢いパスの変異戦略を適用したほうがいいっぽい
const double MutateDistance = 0.05;
struct Sampler {
private:
	inline double mutate(double value) {
		value += MutateDistance * (2.0 * rand01() - 1.0);
		if (value > 1.0) value -= 1.0;
		if (value < 0.0) value += 1.0;
		return value;
	}
public:
	int used_rand_coords;

	std::vector<double> primary_samples;
	Sampler() {
		used_rand_coords = 0;
		primary_samples.resize(32);
		for (int i = 0; i < 32; i ++)
			primary_samples[i] = rand01();
	}
	void reset() {
		used_rand_coords = 0;
	}

	// 通常の乱数のかわりにこれを使う
	inline double next() {
		if (primary_samples.size() <= used_rand_coords) {
			const int now_max = primary_samples.size();
			primary_samples.resize(primary_samples.size() * 1.5); // 拡張する
			for (int i = now_max; i < primary_samples.size(); i ++)
				primary_samples[i] = rand01();
		}
		used_rand_coords ++;
		return primary_samples[used_rand_coords - 1];
	}

	void mutate() {
		for (int i = 0; i < primary_samples.size(); i ++)
			primary_samples[i] = mutate(primary_samples[i]);
	}
};


double luminance(const Color &color) {
	return Dot(Vec(0.2126, 0.7152, 0.0722), color);
}

// 光源上の点をサンプリングして直接光を計算する
Color direct_radiance_sample(const Vec &v0, const Vec &normal, const int id, Sampler &sampler) {
	// 光源上の一点をサンプリングする
	const double r1 = 2 * PI * sampler.next();
	const double r2 = 1.0 - 2.0 * sampler.next();
	const Vec light_pos = spheres[LightID].position + ((spheres[LightID].radius + EPS) * Vec(sqrt(1.0 - r2*r2) * cos(r1), sqrt(1.0 - r2*r2) * sin(r1), r2));
	
	// サンプリングした点から計算
	const Vec light_normal = Normalize(light_pos - spheres[LightID].position);
	const Vec light_dir = Normalize(light_pos - v0);
	const double dist2 = (light_pos - v0).LengthSquared();
	const double dot0 = Dot(normal, light_dir);
	const double dot1 = Dot(light_normal, -1.0 * light_dir);

	if (dot0 >= 0 && dot1 >= 0) {
		const double G = dot0 * dot1 / dist2;
		double t; // レイからシーンの交差 位置までの距離
		int id_; // 交差したシーン内オブジェクトのID
		intersect_scene(Ray(v0, light_dir), &t, &id_);
		if (fabs(sqrt(dist2) - t) < 1e-3) {		
			return Multiply(spheres[id].color, spheres[LightID].emission) * (1.0 / PI) * G / (1.0 / (4.0 * PI * pow(spheres[LightID].radius, 2.0)));
		}
	}
	return Color();
}

// ray方向からの放射輝度を求める
// ただし、rand01()の代わりにSampler::next()を使う。
// それ以外は普通のパストレースと同じ。
// ただし、各点で明示的に光源の影響をサンプリングして求める。
Color radiance(const Ray &ray, const int depth, Sampler &sampler) {
	double t; // レイからシーンの交差位置までの距離
	int id;   // 交差したシーン内オブジェクトのID
	if (!intersect_scene(ray, &t, &id))
		return BackgroundColor;

	const Sphere &obj = spheres[id];
	const Vec hitpoint = ray.org + t * ray.dir; // 交差位置
	const Vec normal  = Normalize(hitpoint - obj.position); // 交差位置の法線
	const Vec orienting_normal = Dot(normal, ray.dir) < 0.0 ? normal : (-1.0 * normal); // 交差位置の法線（物体からのレイの入出を考慮）
	// 色の反射率最大のものを得る。ロシアンルーレットで使う。
	// ロシアンルーレットの閾値は任意だが色の反射率等を使うとより良い。
	double russian_roulette_probability = std::max(obj.color.x, std::max(obj.color.y, obj.color.z));
	// 一定以上レイを追跡したらロシアンルーレットを実行し追跡を打ち切るかどうかを判断する
	if (depth > MaxDepth) {
		if (sampler.next() >= russian_roulette_probability)
			return Color();
	} else
		russian_roulette_probability = 1.0; // ロシアンルーレット実行しなかった

	switch (obj.ref_type) {
	case DIFFUSE: {
		// 直接光のサンプリングを行う
		if (id != LightID) {
			const int shadow_ray = 1;
			Vec direct_light;
			for (int i = 0; i < shadow_ray; i ++) {
				direct_light = direct_light + direct_radiance_sample(hitpoint, orienting_normal, id, sampler) / shadow_ray;
			}

			// orienting_normalの方向を基準とした正規直交基底(w, u, v)を作る。この基底に対する半球内で次のレイを飛ばす。
			Vec w, u, v;
			w = orienting_normal;
			if (fabs(w.x) > 0.1)
				u = Normalize(Cross(Vec(0.0, 1.0, 0.0), w));
			else
				u = Normalize(Cross(Vec(1.0, 0.0, 0.0), w));
			v = Cross(w, u);
			// コサイン項を使った重点的サンプリング
			const double r1 = 2 * PI * sampler.next();
			const double r2 = sampler.next(), r2s = sqrt(r2);
			Vec dir = Normalize((u * cos(r1) * r2s + v * sin(r1) * r2s + w * sqrt(1.0 - r2)));

			return (direct_light + Multiply(obj.color, radiance(Ray(hitpoint, dir), depth+1, sampler))) / russian_roulette_probability;
		} else if (depth == 0) {
			return obj.emission;
		} else
			return Color();
	} break;
	case SPECULAR: {
		// 完全鏡面なのでレイの反射方向は決定的。
		// ロシアンルーレットの確率で除算するのは上と同じ。
		double lt;
		int lid;
		Ray reflection_ray = Ray(hitpoint, ray.dir - normal * 2.0 * Dot(normal, ray.dir));
		intersect_scene(reflection_ray, &lt, &lid);
		Vec direct_light;
		if (lid == LightID)
			direct_light = spheres[LightID].emission;

		return (direct_light + Multiply(obj.color, radiance(reflection_ray, depth+1, sampler))) / russian_roulette_probability;
	} break;
	case REFRACTION: {
		Ray reflection_ray = Ray(hitpoint, ray.dir - normal * 2.0 * Dot(normal, ray.dir));
		
		// 反射方向からの直接光
		double lt;
		int lid;
		intersect_scene(reflection_ray, &lt, &lid);
		Vec direct_light;
		if (lid == LightID)
			direct_light = spheres[LightID].emission;

		bool into = Dot(normal, orienting_normal) > 0.0; // レイがオブジェクトから出るのか、入るのか

		// Snellの法則
		const double nc = 1.0; // 真空の屈折率
		const double nt = 1.5; // オブジェクトの屈折率
		const double nnt = into ? nc / nt : nt / nc;
		const double ddn = Dot(ray.dir, orienting_normal);
		const double cos2t = 1.0 - nnt * nnt * (1.0 - ddn * ddn);
		
		if (cos2t < 0.0) { // 全反射した
			return (direct_light + Multiply(obj.color, (radiance(reflection_ray, depth+1, sampler)))) / russian_roulette_probability;
		}
		// 屈折していく方向
		Vec tdir = Normalize(ray.dir * nnt - normal * (into ? 1.0 : -1.0) * (ddn * nnt + sqrt(cos2t)));

		// SchlickによるFresnelの反射係数の近似
		const double a = nt - nc, b = nt + nc;
		const double R0 = (a * a) / (b * b);
		const double c = 1.0 - (into ? -ddn : Dot(tdir, normal));
		const double Re = R0 + (1.0 - R0) * pow(c, 5.0);
		const double Tr = 1.0 - Re; // 屈折光の運ぶ光の量
		const double probability  = 0.25 + 0.5 * Re;
		
		// 屈折方向からの直接光
		Ray refraction_ray = Ray(hitpoint, tdir);
		intersect_scene(refraction_ray, &lt, &lid);
		Vec direct_light_refraction;
		if (lid == LightID)
			direct_light_refraction = spheres[LightID].emission;

		// 一定以上レイを追跡したら屈折と反射のどちらか一方を追跡する。（さもないと指数的にレイが増える）
		// ロシアンルーレットで決定する。
		if (depth > 2) {
			if (sampler.next() < probability) { // 反射
				return  Multiply(obj.color, (direct_light + radiance(reflection_ray, depth+1, sampler)) * Re)
					/ probability
					/ russian_roulette_probability;
			} else { // 屈折
				return  Multiply(obj.color, (direct_light_refraction + radiance(refraction_ray, depth+1, sampler)) * Tr)
					/ (1.0 - probability) 
					/ russian_roulette_probability;
			}
		} else { // 屈折と反射の両方を追跡
			return Multiply(obj.color, (direct_light + radiance(reflection_ray, depth+1, sampler)) * Re
				                  + (direct_light_refraction + radiance(refraction_ray, depth+1, sampler)) * Tr) / russian_roulette_probability;
		}
	} break;
	}

	return Color();
}

// 上のパストレで生成したパスを保存しておく
struct PathSample {
	int x, y;
	bool direct_hit;
	Color F;

	PathSample(const int x_ = 0, const int y_ = 0, const Color &F_ = Color(), const bool direct_hit_ = false) :
	x(x_), y(y_), F(F_), direct_hit(direct_hit_) {}
};

// ERPTのために新しいパスをサンプリングする関数。
PathSample generate_new_path(const Ray &camera, const Vec &cx, const Vec &cy, const int width, const int height, Sampler &sampler, int x, int y, bool image_plane_mutation = false) {
	const double weight = 4.0; // 2x2サブサンプリングの分の重み
	const int image_plane_mutation_value = 10; // スクリーン上でのパスの変異量

	// スクリーン上で変異する
	double s1 = sampler.next();
	double s2 = sampler.next();

	if (image_plane_mutation) {
		x += int(image_plane_mutation_value * 2 * s1 - image_plane_mutation_value + 0.5);
		y += int(image_plane_mutation_value * 2 * s2 - image_plane_mutation_value + 0.5); 
	}
	if (x < 0 || width <= x || y < 0 || height <= y) 
		return PathSample(0, 0, 0, 0);

	// 2x2サブサンプリング
	int sx = sampler.next() < 0.5 ? 0 : 1;
	int sy = sampler.next() < 0.5 ? 0 : 1;
	
	// テントフィルターによってサンプリング
	// ピクセル範囲で一様にサンプリングするのではなく、ピクセル中央付近にサンプルがたくさん集まるように偏りを生じさせる
	const double r1 = 2.0 * sampler.next(), dx = r1 < 1.0 ? sqrt(r1) - 1.0 : 1.0 - sqrt(2.0 - r1);
	const double r2 = 2.0 * sampler.next(), dy = r2 < 1.0 ? sqrt(r2) - 1.0 : 1.0 - sqrt(2.0 - r2);
	Vec dir = cx * (((sx + 0.5 + dx) / 2.0 + x) / width - 0.5) +
				cy * (((sy + 0.5 + dy) / 2.0 + y) / height- 0.5) + camera.dir;
	const Ray ray = Ray(camera.org + dir * 130.0, Normalize(dir));

	// パストレして放射輝度推定
	const Color c = weight * radiance(ray, 0, sampler);
	sampler.reset();

	// 光源に直接ヒットしたっぽい場合、そのフラグを立てる
	if (fabs(luminance(c - weight * spheres[LightID].emission)) < EPS) {
		return PathSample(x, y, c, true);
	}

	return PathSample(x, y, c);
}



// ERPTする
void render_erpt(const int samples, const int mutation, Color *image, const Ray &camera, const Vec &cx, const Vec &cy, const int width, const int height) {
	Color sumI;

	// edを求める
	for (int y = 0; y < height; y ++) {
		for (int x = 0; x < width; x ++) {
			Sampler X;
			sumI = sumI + generate_new_path(camera, cx, cy, width, height, X, x, y).F;
		}
	}
	const double ed = luminance(sumI / (width * height)) / mutation;
	
	// OpenMP
	omp_lock_t lock0;
	omp_init_lock(&lock0);
#pragma omp parallel for schedule(dynamic, 1)
	for (int y = 0; y < height; y ++) {	
		std::vector<Color> tmp_image;
		tmp_image.resize(width * height);

		std::cerr << "Rendering " << (100.0 * y / (height - 1)) << "%" << std::endl;
		srand(y * y * y);

		for (int x = 0; x < width; x ++) {
			for (int i = 0; i < samples; i ++) {
				Sampler X;

				// 現在のスクリーン上のある点からのパスによる放射輝度を求める
				PathSample new_sample = generate_new_path(camera, cx, cy, width, height, X, x, y);
				const Color e = new_sample.F;

				// パスが光源に直接ヒットしてた場合、エネルギー分配しないで、そのまま画像に送る
				if (new_sample.direct_hit) {
					tmp_image[new_sample.y * width + new_sample.x] = tmp_image[new_sample.y * width + new_sample.x] + new_sample.F / samples;
					continue;
				}

				// この辺は論文と同じ
				if (luminance(e) > 0.0) {
					const int numChains = std::floor(rand01() + luminance(e) / (mutation * ed));

					// Implementing Energy Redistribution Path Tracing参照
					// 周囲に分配するエネルギーがこれ
					const Color dep_value = e / luminance(e) * ed / samples;

					for (int nc = 0; nc < numChains; nc ++) {
						Sampler Y = X;
						PathSample Ypath = new_sample;

						// Consecutive sample filtering
						// ある点に極端にエネルギーが分配されると、スポットノイズになってしまう。
						// Unbiasedにするにはそれも仕方ないが、現実的には見苦しいのである点に対する分配回数を制限することでそのようなノイズを抑える
						// Biasedになるが、見た目は良くなる
						const int MaxStack = 10;
						int stack_num = 0;
						int now_x = x, now_y = y;

						for (int im = 0; im < mutation; im ++) {
							Sampler Z = Y; Z.mutate();
							PathSample Zpath = generate_new_path(camera, cx, cy, width, height, Z, x, y, true);

							const double lfz = luminance(Zpath.F);
							const double lfy = luminance(Ypath.F);
							const double q = lfz / lfy;
							if (q > rand01()) {
								Y = Z;
								Ypath = Zpath;
							}

							// Consecutive sample filtering
							if (now_x == Ypath.x && now_y == Ypath.y) {
								stack_num ++;
							} else {
								now_x = Ypath.x;
								now_y = Ypath.y;
								stack_num = 0;
							}
							
							// エネルギーをRedistributionする
							if (stack_num < MaxStack)
								tmp_image[Ypath.y * width + Ypath.x] = tmp_image[Ypath.y * width + Ypath.x] + dep_value;
						}
					}
				}
			}
		}

		// OpenMP
		omp_set_lock(&lock0);
		for(int i = 0; i < width * height; i ++) {
			image[i] = image[i] + tmp_image[i];
		}
		omp_unset_lock(&lock0);
	}
	
	// OpenMP
	omp_destroy_lock(&lock0);
}

// *** .hdrフォーマットで出力するための関数 ***
struct HDRPixel {
	unsigned char r, g, b, e;
	HDRPixel(const unsigned char r_ = 0, const unsigned char g_ = 0, const unsigned char b_ = 0, const unsigned char e_ = 0) :
	r(r_), g(g_), b(b_), e(e_) {};
	unsigned char get(int idx) {
		switch (idx) {
		case 0: return r;
		case 1: return g;
		case 2: return b;
		case 3: return e;
		} return 0;
	}

};

// doubleのRGB要素を.hdrフォーマット用に変換
HDRPixel get_hdr_pixel(const Color &color) {
	double d = std::max(color.x, std::max(color.y, color.z));
	if (d <= 1e-32)
		return HDRPixel();
	int e;
	double m = frexp(d, &e); // d = m * 2^e
	d = m * 256.0 / d;
	return HDRPixel(color.x * d, color.y * d, color.z * d, e + 128);
}

// 書き出し用関数
void save_hdr_file(const std::string &filename, const Color* image, const int width, const int height) {
	FILE *fp = fopen(filename.c_str(), "wb");
	if (fp == NULL) {
		std::cerr << "Error: " << filename << std::endl;
		return;
	}
	// .hdrフォーマットに従ってデータを書きだす
	// ヘッダ
	unsigned char ret = 0x0a;
	fprintf(fp, "#?RADIANCE%c", (unsigned char)ret);
	fprintf(fp, "# Made with 100%% pure HDR Shop%c", ret);
	fprintf(fp, "FORMAT=32-bit_rle_rgbe%c", ret);
	fprintf(fp, "EXPOSURE=1.0000000000000%c%c", ret, ret);

	// 輝度値書き出し
	fprintf(fp, "-Y %d +X %d%c", height, width, ret);
	for (int i = height - 1; i >= 0; i --) {
		std::vector<HDRPixel> line;
		for (int j = 0; j < width; j ++) {
			HDRPixel p = get_hdr_pixel(image[j + i * width]);
			line.push_back(p);
		}
		fprintf(fp, "%c%c", 0x02, 0x02);
		fprintf(fp, "%c%c", (width >> 8) & 0xFF, width & 0xFF);
		for (int i = 0; i < 4; i ++) {
			for (int cursor = 0; cursor < width;) {
				const int cursor_move = std::min(127, width - cursor);
				fprintf(fp, "%c", cursor_move);
				for (int j = cursor;  j < cursor + cursor_move; j ++)
					fprintf(fp, "%c", line[j].get(i));
				cursor += cursor_move;
			}
		}
	}

	fclose(fp);
}
#include <time.h>
int main(int argc, char **argv) {
	int width = 320;
	int height = 240;
	int mutation = 100; // ピクセルあたりの変異数
	int samples = 10; // ピクセルあたりのサンプル

	// カメラ位置
	Ray camera(Vec(50.0, 52.0, 295.6), Normalize(Vec(0.0, -0.042612, -1.0)));
	// シーン内でのスクリーンのx,y方向のベクトル
	Vec cx = Vec(width * 0.5135 / height);
	Vec cy = Normalize(Cross(cx, camera.dir)) * 0.5135;
	Color *image = new Color[width * height];
	
	render_erpt(samples, mutation, image, camera, cx, cy, width, height);
	
	// .hdrフォーマットで出力
	char buf[256];

	sprintf(buf, "%04d_%d.hdr", mutation, time(NULL));
	save_hdr_file(buf, image, width, height);
}

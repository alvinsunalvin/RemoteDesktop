#ifndef IMAGE_H
#define IMAGE_H
#include "Rect.h"
#include <vector>
#include <mutex>

namespace RemoteDesktop{
	namespace Image_Settings{
		extern int Quality;
		extern bool GrazyScale;
	}

	class Image{

		std::vector<char> data;
	public:

		Image() {}		
		explicit Image(char* d, int h, int w) :Pixel_Stride(4), Height(h), Width(w) { data.resize(Pixel_Stride*Height*Width); memcpy(data.data(), d, Pixel_Stride); }
		explicit Image(int h, int w) : Pixel_Stride(4), Height(h), Width(w)  { data.resize(Pixel_Stride*Height*Width); }
		static Image Create_from_Compressed_Data(char* d, int size_in_bytes, int h, int w);
		void Compress();
		void Decompress();
		Image Clone() const;
		//mainly used for image validation
		void Save(std::string outfile);
		
		char* get_Data() { return data.data(); }
		size_t size_in_bytes() const { return data.size(); }
		int Height = 0;
		int Width = 0;
		//pixel stride
		const int Pixel_Stride = 4;
		bool Compressed = false;

		static Rect Difference(Image first, Image second);
		static Image Copy(Image src_img, Rect r);
		static void Copy(Image src_img, int dst_left, int dst_top, int dst_stride, char* dst, int dst_height, int dst_width);
		

	};

	void SaveBMP(BITMAPINFOHEADER bi, char* imgdata, std::string dst = "capture.bmp");
};
#endif
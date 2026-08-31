#include <format>
#include <iostream>

#include <color.h>

int main() {

  auto image_width = 256;
  auto image_height = 256;

  std::cout << std::format("P3\n{} {}\n255\n", image_width, image_height);

  for (auto j = 0; j < image_height; j++) {

    std::clog << std::format("\rScanlines remaining: {} ", image_height - j);

    for (auto i = 0; i < image_width; i++) {

      auto r = double(i) / (image_width - 1);
      auto g = double(j) / (image_height - 1);
      auto b = 0.0;

      auto pixel_color = color<double>(r, g, b);
      write_color(std::cout, pixel_color);
    }
  }

  std::clog << "\rDone.                   \n";
}

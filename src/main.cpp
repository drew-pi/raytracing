
#include <memory>
#include <utility.h>

#include <camera.h>
#include <hittable.h>
#include <hittable_list.h>
#include <material.h>
#include <sphere.h>
#include <vec3.h>

int main() {

  // set up the world
  HittableList<double> world;

  auto material_ground =
      std::make_unique<Lambertian<double>>(Color(0.8, 0.8, 0.0));
  auto material_center =
      std::make_unique<Lambertian<double>>(Color(0.1, 0.2, 0.5));
  auto material_left = std::make_unique<Metal<double>>(Color(0.8, 0.8, 0.8));
  auto material_right = std::make_unique<Metal<double>>(Color(0.8, 0.6, 0.2));

  world.add(std::make_unique<Sphere<double>>(point3(0.0, -100.5, -1.0), 100.0,
                                             std::move(material_ground)));
  world.add(std::make_unique<Sphere<double>>(point3(0.0, 0.0, -1.2), 0.5,
                                             std::move(material_center)));
  world.add(std::make_unique<Sphere<double>>(point3(-1.0, 0.0, -1.0), 0.5,
                                             std::move(material_left)));
  world.add(std::make_unique<Sphere<double>>(point3(1.0, 0.0, -1.0), 0.5,
                                             std::move(material_right)));

  Camera<double> camera;

  camera.aspect_ratio = 16.0 / 9.0;
  camera.image_width = 400;
  camera.samples_per_pixel = 100;
  camera.max_depth = 50;

  camera.render(world);
}

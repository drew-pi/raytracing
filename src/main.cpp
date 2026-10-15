
#include "scene.h"
#include <bvh.h>

int main() {

  rng::seed(12345, 0);

  // std::cout << "BVH entry size: " << sizeof(BVHEntry<double>) << " bytes\n";
  // std::cout << "BVH entry alignment: " << alignof(BVHEntry<double>)
  //           << " bytes\n";

  // std::cout << std::flush;

  // // set up the world
  // HittableList<double> world;

  // auto R = std::cos(std::numbers::pi_v<double> / 4);

  // auto material_left =
  //     std::make_unique<Lambertian<double>>(Color(0.0, 0.0, 1.0));
  // auto material_right =
  //     std::make_unique<Lambertian<double>>(Color(1.0, 0.0, 0.0));

  // world.add(std::make_unique<Sphere<double>>(point3(-R, 0.0, -1.0), R,
  //                                            std::move(material_left)));
  // world.add(std::make_unique<Sphere<double>>(point3(R, 0.0, -1.0), R,
  //                                            std::move(material_right)));

  // auto material_ground =
  //     std::make_unique<Lambertian<double>>(Color(0.8, 0.8, 0.0));
  // auto material_center =
  //     std::make_unique<Lambertian<double>>(Color(0.1, 0.2, 0.5));
  // auto material_left = std::make_unique<Metal<double>>(Color(0.8, 0.8, 0.8));
  // auto material_right = std::make_unique<Metal<double>>(Color(0.8, 0.6,
  // 0.2));

  // world.add(std::make_unique<Sphere<double>>(point3(0.0, -100.5, -1.0),
  // 100.0,
  //                                            std::move(material_ground)));
  // world.add(std::make_unique<Sphere<double>>(point3(0.0, 0.0, -1.2), 0.5,
  //                                            std::move(material_center)));
  // world.add(std::make_unique<Sphere<double>>(point3(-1.0, 0.0, -1.0), 0.5,
  //                                            std::move(material_left)));
  // world.add(std::make_unique<Sphere<double>>(point3(1.0, 0.0, -1.0), 0.5,
  //                                            std::move(material_right)));

  // Camera<double> camera;

  // camera.aspect_ratio = 16.0 / 9.0;
  // camera.image_width = 400;
  // camera.samples_per_pixel = 100;
  // camera.max_depth = 50;

  // camera.vfov = 100;
  // camera.look_from = point3<double>(-2, 2, 1);
  // camera.look_at = point3<double>(0, 0, -1);
  // camera.v_up = vec3<double>(0, 1, 0);

  HittableList<double> world;

  world.add(std::make_unique<Sphere<double>>(
      point3<double>(0, -100.5, -2), 100.0,
      std::make_unique<Lambertian<double>>(Color<double>(0.5, 0.5, 0.5))));

  // Sharp reference sphere.
  world.add(std::make_unique<Sphere<double>>(
      point3<double>(0, 0, -3), 0.4,
      std::make_unique<Lambertian<double>>(Color<double>(0.2, 0.3, 0.9))));

  // Moves left to right during the camera's [0, 1) exposure.
  world.add(std::make_unique<Sphere<double>>(
      point3<double>(-0.8, 0.0, -1.5), point3<double>(-0.8, -0.2, -1.5), 0.3,
      std::make_unique<Lambertian<double>>(Color<double>(0.9, 0.15, 0.1))));

  Camera<double> camera;
  camera.aspect_ratio = 16.0 / 9.0;
  camera.image_width = 803;
  camera.samples_per_pixel = 300;
  camera.max_depth = 6;
  camera.vfov = 60;

  camera.tile_renderer(world);

  // auto [camera, world] = create_scene();
  // BVHNode<double> bvh(std::move(world));
  // camera.render(bvh);
}

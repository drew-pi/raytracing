Before building or executing must keep the cmake configuration up to date

```bash
cmake -S . -B build \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

To build the project run the following command from the root of the project

`cmake --build build`

Then to run the executable use

`./build/raytracer > image.ppm`

## In order to compile for speed use

```bash
cmake -S . -B release \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native"
```

`cmake --build release --parallel`

`./release/raytracer > image.ppm`

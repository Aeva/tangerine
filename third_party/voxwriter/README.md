# MagicaVoxel File Writer

vox.hm is the file format descriptor for HexaMonkey :
- original topic about it : https://github.com/ephtracy/voxel-model/issues/19
- HexaMonkey tool : http://hexamonkey.com/

## App

the main.cpp file show you how to generate a quick file :

With this simple code (thanks to [@unphased](https://github.com/unphased))

```cpp
#include "VoxWriter.h"
int main() 
{
	vox::VoxWriter vox;
	for (int i = 0; i < 1000; ++i) {
		for (int j = 0; j < 1000; ++j) {
			vox.AddVoxel(i, j, (int)std::floor(sinf((float)(i * i + j * j) / 50000) * 150) + 150, (i + j) % 255 + 1);
		}
	}
	vox.SaveToFile("output_voxwriter.vox");
}
```

you can generate that (previewed in [Magicavoxel](https://ephtracy.github.io/)

![main](main.jpg)

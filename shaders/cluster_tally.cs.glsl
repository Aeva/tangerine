prepend: shaders/defines.h
--------------------------------------------------------------------------------

// Copyright 2021 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissionsand
// limitations under the License.


layout(std430, binding = 0) restrict writeonly buffer TileDrawArgs
{
	uint PrimitiveCount;
	uint InstanceCount;
	uint First;
	uint BaseInstance;
	uint InstanceOffset; // Not a draw param.
};


layout(std140, binding = 1) restrict buffer TileHeapInfo
{
	uint HeapSize;
	uint SegmentStart;
	uint StackPtr;
};


// Just a simple setup for now...
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main()
{
	if (SegmentStart < HeapSize)
	{
		uint SegmentEnd = min(HeapSize, StackPtr);
		uint SegmentSize = SegmentEnd - SegmentStart;
		PrimitiveCount = 6;
		InstanceCount = SegmentSize;
		First = 0;
		BaseInstance = 0;
		InstanceOffset = SegmentStart;
		SegmentStart = SegmentEnd;
	}
	else
	{
		PrimitiveCount = 0;
		InstanceCount = 0;
		First = 0;
		BaseInstance = 0;
	}
}

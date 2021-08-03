
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


float SphereBrush(vec3 Point, float Radius)
{
	return length(Point) - Radius;
}


float UnionOp(float LHS, float RHS)
{
	return min(LHS, RHS);
}


float IntersectionOp(float LHS, float RHS)
{
	return max(LHS, RHS);
}


float CutOp(float LHS, float RHS)
{
	return max(LHS, -RHS);
}



float SmoothUnionOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS - RHS), 0.0);
	return min(LHS, RHS) - H * H * 0.25 / Threshold;
}


float SmoothIntersectionOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS - RHS), 0.0);
	return max(LHS, RHS) + H * H * 0.25 / Threshold;
}


float SmoothCutOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS + RHS), 0.0);
	return max(LHS, -RHS) + H * H * 0.25 / Threshold;
}

--------------------------------------------------------------------------------

// Copyright 2022 Aeva Palecek
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
// See the License for the specific language governing permissions and
// limitations under the License.


#ifdef INTERPRETED

#ifndef INTERPRETER_STACK
#error "Missing required define: INTERPRETER_STACK"
#endif

MaterialDist Interpret(const vec3 EvalPoint)
{
	MaterialDist Stack[INTERPRETER_STACK];
	for (int i = 0; i < INTERPRETER_STACK; ++i)
	{
		Stack[i] = MaterialDist(vec3(1.0), 0.0);
	}

	uint StackPointer = 0;
	uint ProgramCounter = 0;
	vec3 Point = EvalPoint;

	while (true)
	{
		const uint Opcode = floatBitsToUint(PARAMS[ProgramCounter++]);

		if (StackPointer >= INTERPRETER_STACK)
		{
			// The stack pointer is out of bounds.  Halt and catch fire.
			return MaterialDist(vec3(1.0, 1.0, 0.0), 0.0);
		}

		// Set operators
		else if (Opcode < OPCODE_SPHERE)
		{
			--StackPointer;

			if (Opcode < OPCODE_SMOOTH)
			{
				if (Opcode == OPCODE_UNION)
				{
					Stack[StackPointer] = UnionOp(Stack[StackPointer], Stack[StackPointer + 1]);
					continue;
				}
				else if (Opcode == OPCODE_INTER)
				{
					Stack[StackPointer] = InterOp(Stack[StackPointer], Stack[StackPointer + 1]);
					continue;
				}
				else if (Opcode == OPCODE_DIFF)
				{
					Stack[StackPointer] = DiffOp(Stack[StackPointer], Stack[StackPointer + 1]);
					continue;
				}
			}
			else
			{
				float Threshold = PARAMS[ProgramCounter++];
				if (Opcode == OPCODE_SMOOTH_UNION)
				{
					Stack[StackPointer] = SmoothUnionOp(Stack[StackPointer], Stack[StackPointer + 1], Threshold);
					continue;
				}
				else if (Opcode == OPCODE_SMOOTH_INTER)
				{
					Stack[StackPointer] = SmoothInterOp(Stack[StackPointer], Stack[StackPointer + 1], Threshold);
					continue;
				}
				else if (Opcode == OPCODE_SMOOTH_DIFF)
				{
					Stack[StackPointer] = SmoothDiffOp(Stack[StackPointer], Stack[StackPointer + 1], Threshold);
					continue;
				}
			}
		}

		// Brush operands
		else if (Opcode < OPCODE_OFFSET)
		{
			if (Opcode == OPCODE_SPHERE)
			{
				Stack[StackPointer].Dist = SphereBrush(Point,
					PARAMS[ProgramCounter++]);
				Point = EvalPoint;
				continue;
			}
			else if (Opcode == OPCODE_ELLIPSOID)
			{
				Stack[StackPointer].Dist = EllipsoidBrush(Point,
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++]);
				Point = EvalPoint;
				continue;
			}
			else if (Opcode == OPCODE_BOX)
			{
				Stack[StackPointer].Dist = BoxBrush(Point,
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++]);
				Point = EvalPoint;
				continue;
			}
			else if (Opcode == OPCODE_TORUS)
			{
				Stack[StackPointer].Dist = TorusBrush(Point,
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++]);
				Point = EvalPoint;
				continue;
			}
			else if (Opcode == OPCODE_CYLINDER)
			{
				Stack[StackPointer].Dist = CylinderBrush(Point,
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++]);
				Point = EvalPoint;
				continue;
			}
			else if (Opcode == OPCODE_PLANE)
			{
				Stack[StackPointer].Dist = Plane(Point,
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++]);
				Point = EvalPoint;
				continue;
			}
		}

		// Misc
		else
		{
			if (Opcode == OPCODE_OFFSET)
			{
				vec3 Offset = vec3(
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++]);
				Point -= Offset;
				continue;
			}
			else if (Opcode == OPCODE_MATRIX)
			{
				mat4 Matrix = mat4(
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++],
					PARAMS[ProgramCounter++]);
				Point = MatrixTransform(Point, Matrix);
				continue;
			}
			else if (Opcode == OPCODE_PAINT)
			{
				Stack[StackPointer].Color.r = PARAMS[ProgramCounter++];
				Stack[StackPointer].Color.g = PARAMS[ProgramCounter++];
				Stack[StackPointer].Color.b = PARAMS[ProgramCounter++];
				continue;
			}
			else if (Opcode == OPCODE_PUSH)
			{
				++StackPointer;
				Stack[StackPointer].Color = vec3(1.0);
				continue;
			}
			else if (Opcode == OPCODE_RETURN)
			{
				return Stack[0];
			}
			else
			{
				// Unknown opcode.  Halt and catch fire.
				return MaterialDist(vec3(1.0, 0.0, 0.0), 0.0);
			}
		}
	}
}
#endif

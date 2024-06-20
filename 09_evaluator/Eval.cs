using System;
using System.Numerics;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;
using Vector2 = System.Numerics.Vector2;
using Vector3 = System.Numerics.Vector3;


namespace Evaluator;


public enum Opcode : uint
{
    Stop = 0,

    Sphere,
    Ellipsoid,
    Box,
    Torus,
    Cylinder,
    Cone,
    Coninder,
    Plane,

    Union,
    Inter,
    Diff,
    BlendUnion,
    BlendInter,
    BlendDiff,
    Flate,

    Move,
    RotateX,
    RotateY,
    RotateZ,
    ScaleField,
}


[System.Runtime.InteropServices.StructLayout(LayoutKind.Explicit)]
public struct ProgramWord
{
    [System.Runtime.InteropServices.FieldOffset(0)]
    public Opcode Symbol;

    [System.Runtime.InteropServices.FieldOffset(0)]
    public float Value;
}


public class ProgramBuffer
{
    private ProgramWord[] Words;
    public long WordCount => Words.Length;

    public ProgramWord this[long Index]
    {
        get => Words[Index];
    }

    private long _StackSize;
    public long StackSize => _StackSize;

    private ProgramBuffer(long WordCount)
    {
        Words = new ProgramWord[WordCount];
        _StackSize = 1;
    }

    private ProgramBuffer(ProgramBuffer CopyTarget, long PrependCount, long AppendCount, out long AppendCursor)
    {
        AppendCursor = PrependCount + CopyTarget.WordCount;
        Words = new ProgramWord[AppendCursor + AppendCount];
        Array.Copy(CopyTarget.Words, 0, Words, PrependCount, CopyTarget.WordCount);
        _StackSize = CopyTarget.StackSize;
    }

    private ProgramBuffer(ProgramBuffer CopyLHS, ProgramBuffer CopyRHS, long AppendCount, out long AppendCursor)
    {
        AppendCursor = CopyLHS.WordCount + CopyRHS.WordCount;
        Words = new ProgramWord[AppendCursor + AppendCount];
        Array.Copy(CopyLHS.Words, 0, Words, 0, CopyLHS.WordCount);
        Array.Copy(CopyRHS.Words, 0, Words, CopyLHS.WordCount, CopyRHS.WordCount);
        _StackSize = Math.Max(CopyLHS.StackSize, CopyRHS.StackSize + 1);
    }

    public static ProgramBuffer Sphere(float Diameter)
    {
        var Kernel = new ProgramBuffer(2);
        Kernel.Words[0].Symbol = Opcode.Sphere;
        Kernel.Words[1].Value = Diameter * 0.5f;
        return Kernel;
    }

    public static ProgramBuffer Ellipsoid(float DiameterX, float DiameterY, float DiameterZ)
    {
        var Kernel = new ProgramBuffer(4);
        Kernel.Words[0].Symbol = Opcode.Ellipsoid;
        Kernel.Words[1].Value = DiameterX * 0.5f;
        Kernel.Words[2].Value = DiameterY * 0.5f;
        Kernel.Words[3].Value = DiameterZ * 0.5f;
        return Kernel;
    }

    public static ProgramBuffer Box(float SpanX, float SpanY, float SpanZ)
    {
        var Kernel = new ProgramBuffer(4);
        Kernel.Words[0].Symbol = Opcode.Box;
        Kernel.Words[1].Value = SpanX * 0.5f;
        Kernel.Words[2].Value = SpanY * 0.5f;
        Kernel.Words[3].Value = SpanZ * 0.5f;
        return Kernel;
    }

    public static ProgramBuffer Cube(float Span)
    {
        return Box(Span, Span, Span);
    }

    public static ProgramBuffer Torus(float MajorDiameter, float MinorDiameter)
    {
        var Kernel = new ProgramBuffer(3);
        Kernel.Words[0].Symbol = Opcode.Torus;
        Kernel.Words[1].Value = MajorDiameter * 0.5f;
        Kernel.Words[2].Value = MinorDiameter * 0.5f;
        return Kernel;
    }

    public static ProgramBuffer Cylinder(float Diameter, float Height)
    {
        var Kernel = new ProgramBuffer(3);
        Kernel.Words[0].Symbol = Opcode.Cylinder;
        Kernel.Words[1].Value = Diameter * 0.5f;
        Kernel.Words[2].Value = Height * 0.5f;
        return Kernel;
    }

    public static ProgramBuffer Plane(float NormalX, float NormalY, float NormalZ)
    {
        var Kernel = new ProgramBuffer(4);
        Kernel.Words[0].Symbol = Opcode.Plane;
        Kernel.Words[1].Value = NormalX;
        Kernel.Words[2].Value = NormalY;
        Kernel.Words[3].Value = NormalZ;
        return Kernel;
    }

    public static ProgramBuffer Union(ProgramBuffer LHS, ProgramBuffer RHS)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(LHS, RHS, 1, out Cursor);
        Kernel.Words[Cursor].Symbol = Opcode.Union;
        return Kernel;
    }

    public static ProgramBuffer Inter(ProgramBuffer LHS, ProgramBuffer RHS)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(LHS, RHS, 1, out Cursor);
        Kernel.Words[Cursor].Symbol = Opcode.Inter;
        return Kernel;
    }

    public static ProgramBuffer Diff(ProgramBuffer LHS, ProgramBuffer RHS)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(LHS, RHS, 1, out Cursor);
        Kernel.Words[Cursor].Symbol = Opcode.Diff;
        return Kernel;
    }

    public static ProgramBuffer Union(ProgramBuffer LHS, ProgramBuffer RHS, float Threshold)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(LHS, RHS, 2, out Cursor);
        Kernel.Words[Cursor].Symbol = Opcode.BlendUnion;
        Kernel.Words[Cursor + 1].Value = Threshold;
        return Kernel;
    }

    public static ProgramBuffer Inter(ProgramBuffer LHS, ProgramBuffer RHS, float Threshold)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(LHS, RHS, 2, out Cursor);
        Kernel.Words[Cursor].Symbol = Opcode.BlendInter;
        Kernel.Words[Cursor + 1].Value = Threshold;
        return Kernel;
    }

    public static ProgramBuffer Diff(ProgramBuffer LHS, ProgramBuffer RHS, float Threshold)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(LHS, RHS, 2, out Cursor);
        Kernel.Words[Cursor].Symbol = Opcode.BlendDiff;
        Kernel.Words[Cursor + 1].Value = Threshold;
        return Kernel;
    }

    public static ProgramBuffer Move(ProgramBuffer Field, float X, float Y, float Z)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(Field, 4, 0, out Cursor);
        Kernel.Words[0].Symbol = Opcode.Move;
        Kernel.Words[1].Value = X;
        Kernel.Words[2].Value = Y;
        Kernel.Words[3].Value = Z;
        return Kernel;
    }

    public static ProgramBuffer RotateX(ProgramBuffer Field, float Degrees)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(Field, 3, 0, out Cursor);
        Kernel.Words[0].Symbol = Opcode.RotateX;
        float Radians = (float)((double)Degrees * Math.PI / 180.0);
        Kernel.Words[1].Value = (float)Math.Sin(Radians);
        Kernel.Words[2].Value = (float)Math.Cos(Radians);
        return Kernel;
    }

    public static ProgramBuffer RotateY(ProgramBuffer Field, float Degrees)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(Field, 3, 0, out Cursor);
        Kernel.Words[0].Symbol = Opcode.RotateY;
        float Radians = (float)((double)Degrees * Math.PI / 180.0);
        Kernel.Words[1].Value = (float)Math.Sin(Radians);
        Kernel.Words[2].Value = (float)Math.Cos(Radians);
        return Kernel;
    }

    public static ProgramBuffer RotateZ(ProgramBuffer Field, float Degrees)
    {
        long Cursor;
        var Kernel = new ProgramBuffer(Field, 3, 0, out Cursor);
        Kernel.Words[0].Symbol = Opcode.RotateZ;
        float Radians = (float)((double)Degrees * Math.PI / 180.0);
        Kernel.Words[1].Value = (float)Math.Sin(Radians);
        Kernel.Words[2].Value = (float)Math.Cos(Radians);
        return Kernel;
    }
}


public class Interpreter
{
    private readonly ProgramBuffer Program;

    public Interpreter(ProgramBuffer InProgram)
    {
        Program = InProgram;
    }

    public float Eval(Vector3 EvalPoint)
    {
        var Stack = new float[Program.StackSize];
        long ProgramCounter = 0;
        long StackPointer = 0;

        var ReadSymbol = () =>
        {
            return Program[ProgramCounter++].Symbol;
        };

        var ReadValue = () =>
        {
            return Program[ProgramCounter++].Value;
        };

        var ReadVec3 = () =>
        {
            Vector3 Vec;
            Vec.X = ReadValue();
            Vec.Y = ReadValue();
            Vec.Z = ReadValue();
            return Vec;
        };

        var StackPush = (float Dist) =>
        {
            Stack[StackPointer++] = Dist;
        };

        var StackPop = () =>
        {
            return Stack[--StackPointer];
        };

        Vector3 Point = EvalPoint;

        while (ProgramCounter < Program.WordCount)
        {
            switch (ReadSymbol())
            {
                case Opcode.Sphere:
                {
                    float Radius = ReadValue();
                    float Dist = Point.Length() - Radius;
                    StackPush(Dist);
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Box:
                {
                    Vector3 Extent = ReadVec3();
                    Vector3 A = Vector3.Abs(Point) - Extent;
                    Vector3 Zero;
                    Zero.X = 0.0f;
                    Zero.Y = 0.0f;
                    Zero.Z = 0.0f;
                    float Dist = Vector3.Max(A, Zero).Length() + Math.Min(Math.Max(Math.Max(A.X, A.Y), A.Z), 0.0f);
                    StackPush(Dist);
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Cylinder:
                {
                    float Radius = ReadValue();
                    float Extent = ReadValue();

                    Vector2 D;
                    D.X = Point.X;
                    D.Y = Point.Y;
                    D.X = D.Length() - Radius;
                    D.Y = Math.Abs(Point.Z) - Extent;
                    //vec2 D = abs(vec2(length(vec2(Point.xy())), Point.z)) - vec2(Radius, Extent);

                    Vector2 Zero;
                    Zero.X = 0.0f;
                    Zero.Y = 0.0f;
                    // return min(max(D.x, D.y), 0.0) + Vector2.Max(D, Zero).Length();

                    float Dist = Math.Min(Math.Max(D.X, D.Y), 0.0f) + Vector2.Max(D, Zero).Length();
                    StackPush(Dist);
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Plane:
                {
                    Vector3 Normal = ReadVec3();
                    float Dist = Vector3.Dot(Point, Normal);
                    StackPush(Dist);
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Union:
                {
                    float RHS = StackPop();
                    float LHS = StackPop();
                    float Dist = Math.Min(LHS, RHS);
                    StackPush(Dist);
                    break;
                }

                case Opcode.Inter:
                {
                    float RHS = StackPop();
                    float LHS = StackPop();
                    float Dist = Math.Max(LHS, RHS);
                    StackPush(Dist);
                    break;
                }

                case Opcode.Diff:
                {
                    float RHS = StackPop();
                    float LHS = StackPop();
                    float Dist = Math.Max(LHS, -RHS);
                    StackPush(Dist);
                    break;
                }

                case Opcode.BlendUnion:
                {
                    float RHS = StackPop();
                    float LHS = StackPop();
                    float Threshold = ReadValue();
                    float H = Math.Max(Threshold - Math.Abs(LHS - RHS), 0.0f);
                    float Dist = Math.Min(LHS, RHS) - H * H * 0.25f / Threshold;
                    StackPush(Dist);
                    break;
                }

                case Opcode.BlendInter:
                {
                    float RHS = StackPop();
                    float LHS = StackPop();
                    float Threshold = ReadValue();
                    float H = Math.Max(Threshold - Math.Abs(LHS - RHS), 0.0f);
                    float Dist = Math.Max(LHS, RHS) + H * H * 0.25f / Threshold;
                    StackPush(Dist);
                    break;
                }

                case Opcode.BlendDiff:
                {
                    float RHS = StackPop();
                    float LHS = StackPop();
                    float Threshold = ReadValue();
                    float H = Math.Max(Threshold - Math.Abs(LHS + RHS), 0.0f);
                    float Dist = Math.Max(LHS, -RHS) + H * H * 0.25f / Threshold;
                    StackPush(Dist);
                    break;
                }

                case Opcode.Move:
                {
                    Point += ReadVec3();
                    break;
                }

                case Opcode.RotateX:
                {
                    float S = ReadValue();
                    float C = ReadValue();
                    // TODO double check if this is correct
                    float Y = Point.Y * C - Point.Z * S;
                    float Z = Point.Y * S + Point.Z * C;
                    Point.Y = Y;
                    Point.Z = Z;
                    break;
                }

                case Opcode.RotateY:
                {
                    float S = ReadValue();
                    float C = ReadValue();
                    // TODO double check if this is correct
                    float X = Point.X * C - Point.Z * S;
                    float Z = Point.X * S + Point.Z * C;
                    Point.X = X;
                    Point.Z = Z;
                    break;
                }

                case Opcode.RotateZ:
                {
                    float S = ReadValue();
                    float C = ReadValue();
                    float X = Point.X * C - Point.Y * S;
                    float Y = Point.X * S + Point.Y * C;
                    Point.X = X;
                    Point.Y = Y;
                    break;
                }

                default:
                {
                    // Unknown opcode.  Halt and catch fire.
                    Debug.Assert(false);
                    return 0.0f;
                }
            };
        }
        float Result = StackPop();
        Debug.Assert(StackPointer == 0);
        return Result;
    }

    public Vector3 Gradient(Vector3 Point)
    {
        float AlmostZero = 0.0001f;
        var OffsetPNN = new Vector3(AlmostZero, -AlmostZero, -AlmostZero);
        var OffsetNPN = new Vector3(-AlmostZero, AlmostZero, -AlmostZero);
        var OffsetNNP = new Vector3(-AlmostZero, -AlmostZero, AlmostZero);
        var OffsetPPP = new Vector3(AlmostZero, AlmostZero, AlmostZero);

        // Tetrahedral method
        Vector3 Normal =
            OffsetPNN * Eval(Point + OffsetPNN) +
            OffsetNPN * Eval(Point + OffsetNPN) +
            OffsetNNP * Eval(Point + OffsetNNP) +
            OffsetPPP * Eval(Point + OffsetPPP);

        float LengthSquared = Vector3.Dot(Normal, Normal);
        if (LengthSquared == 0.0)
        {
            // Gradient is zero.  Let's try again with a worse method.
            float Dist = Eval(Point);
            return Vector3.Normalize(new Vector3(
                Eval(Point + OffsetPNN) - Dist,
                Eval(Point + OffsetNPN) - Dist,
                Eval(Point + OffsetNNP) - Dist));
        }
        else
        {
            return Normal / (float)Math.Sqrt(LengthSquared);
        }
    }

    public Vector3 Gradient(Microsoft.Xna.Framework.Vector3 Point)
    {
        return Gradient(new Vector3(Point.X, Point.Y, Point.Z));
    }

    public float Eval(Microsoft.Xna.Framework.Vector3 Point)
    {
        return Eval(new Vector3(Point.X, Point.Y, Point.Z));
    }
}

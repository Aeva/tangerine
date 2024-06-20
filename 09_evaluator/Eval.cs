using System;
using System.Numerics;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;
using Vector2 = System.Numerics.Vector2;
using Vector3 = System.Numerics.Vector3;
using Matrix4x4 = System.Numerics.Matrix4x4;
using Quaternion = System.Numerics.Quaternion;


namespace Evaluator;


public enum Opcode : uint
{
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
}


[System.Runtime.InteropServices.StructLayout(LayoutKind.Explicit)]
public struct ProgramWord
{
    [System.Runtime.InteropServices.FieldOffset(0)]
    public Opcode Symbol;

    [System.Runtime.InteropServices.FieldOffset(0)]
    public float Value;
}


public struct Transform
{
    private Quaternion Rotation;
    private Vector3 Translation;
    private float Scalation;

    public Transform()
    {
        Rotation = Quaternion.Identity;
        Translation = Vector3.Zero;
        Scalation = 1.0f;
    }

    public void Reset()
    {
        Rotation = Quaternion.Identity;
        Translation = Vector3.Zero;
        Scalation = 1.0f;
    }

    public void Move(Vector3 OffsetBy)
    {
        Translation += OffsetBy;
    }

    public void Rotate(Quaternion RotateBy)
    {
        Translation = Vector3.Transform(Translation, RotateBy);
        Rotation = RotateBy * Rotation;
    }

    public void Scale(float ScaleBy)
    {
        Translation *= ScaleBy;
        Scalation *= ScaleBy;
    }

    public Matrix4x4 ToMatrix()
    {
        Matrix4x4 RotationMatrix = Matrix4x4.CreateFromQuaternion(Rotation);
        Matrix4x4 TranslationMatrix = Matrix4x4.CreateTranslation(Translation);
        Matrix4x4 ScalationMatrix = Matrix4x4.CreateScale(Scalation);
        // TODO : double check for correct ordering:
        return ScalationMatrix * TranslationMatrix * RotationMatrix;
    }

    public Vector3 Apply(Vector3 Point)
    {
        return Vector3.Transform(Point * Scalation, Rotation) + Translation;
    }

    public Vector3 ApplyInv(Vector3 Point)
    {
        return Vector3.Transform(Point - Translation, Quaternion.Inverse(Rotation)) / Scalation;
    }
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

    private Transform[] BrushTransforms;
    public long BrushCount => BrushTransforms.Length;

    private ProgramBuffer(long WordCount)
    {
        Words = new ProgramWord[WordCount];
        _StackSize = 1;
        BrushTransforms = new Transform[1];
        BrushTransforms[0].Reset();
    }

    private ProgramBuffer(ProgramBuffer CopyTarget)
    {
        Words = new ProgramWord[CopyTarget.WordCount];
        Array.Copy(CopyTarget.Words, 0, Words, 0, CopyTarget.WordCount);
        _StackSize = CopyTarget.StackSize;
        BrushTransforms = new Transform[CopyTarget.BrushCount];
        Array.Copy(CopyTarget.BrushTransforms, 0, BrushTransforms, 0, CopyTarget.BrushCount);
    }

    private ProgramBuffer(ProgramBuffer CopyTarget, long PrependCount, long AppendCount, out long AppendCursor)
    {
        AppendCursor = PrependCount + CopyTarget.WordCount;
        Words = new ProgramWord[AppendCursor + AppendCount];
        Array.Copy(CopyTarget.Words, 0, Words, PrependCount, CopyTarget.WordCount);
        _StackSize = CopyTarget.StackSize;
        BrushTransforms = new Transform[CopyTarget.BrushCount];
        Array.Copy(CopyTarget.BrushTransforms, 0, BrushTransforms, 0, CopyTarget.BrushCount);
    }

    private ProgramBuffer(ProgramBuffer CopyLHS, ProgramBuffer CopyRHS, long AppendCount, out long AppendCursor)
    {
        AppendCursor = CopyLHS.WordCount + CopyRHS.WordCount;
        Words = new ProgramWord[AppendCursor + AppendCount];
        Array.Copy(CopyLHS.Words, 0, Words, 0, CopyLHS.WordCount);
        Array.Copy(CopyRHS.Words, 0, Words, CopyLHS.WordCount, CopyRHS.WordCount);
        _StackSize = Math.Max(CopyLHS.StackSize, CopyRHS.StackSize + 1);

        BrushTransforms = new Transform[CopyLHS.BrushCount + CopyRHS.BrushCount];
        Array.Copy(CopyLHS.BrushTransforms, 0, BrushTransforms, 0, CopyLHS.BrushCount);
        Array.Copy(CopyRHS.BrushTransforms, 0, BrushTransforms, CopyLHS.BrushCount, CopyRHS.BrushCount);
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

    public static ProgramBuffer Move(ProgramBuffer Field, Vector3 Offset)
    {
        var Kernel = new ProgramBuffer(Field);
        for (long Brush = 0; Brush < Kernel.BrushCount; ++Brush)
        {
            Kernel.BrushTransforms[Brush].Move(Offset);
        }
        return Kernel;
    }

    public static ProgramBuffer Move(ProgramBuffer Field, float X, float Y, float Z)
    {
        return Move(Field, new Vector3(X, Y, Z));
    }

    public static ProgramBuffer MoveX(ProgramBuffer Field, float X)
    {
        return Move(Field, new Vector3(X, 0.0f, 0.0f));
    }

    public static ProgramBuffer MoveY(ProgramBuffer Field, float Y)
    {
        return Move(Field, new Vector3(0.0f, Y, 0.0f));
    }

    public static ProgramBuffer MoveZ(ProgramBuffer Field, float Z)
    {
        return Move(Field, new Vector3(0.0f, 0.0f, Z));
    }

    public static ProgramBuffer Rotate(ProgramBuffer Field, Quaternion Rotation)
    {
        var Kernel = new ProgramBuffer(Field);
        for (long Brush = 0; Brush < Kernel.BrushCount; ++Brush)
        {
            Kernel.BrushTransforms[Brush].Rotate(Rotation);
        }
        return Kernel;
    }

    public static ProgramBuffer RotateX(ProgramBuffer Field, float Degrees)
    {
        float Radians = (float)((double)Degrees * Math.PI / 180.0);
        Quaternion Rotation = Quaternion.CreateFromAxisAngle(Vector3.UnitX, Radians);
        return Rotate(Field, Rotation);
    }

    public static ProgramBuffer RotateY(ProgramBuffer Field, float Degrees)
    {
        float Radians = (float)((double)Degrees * Math.PI / 180.0);
        Quaternion Rotation = Quaternion.CreateFromAxisAngle(Vector3.UnitY, Radians);
        return Rotate(Field, Rotation);
    }

    public static ProgramBuffer RotateZ(ProgramBuffer Field, float Degrees)
    {
        float Radians = (float)((double)Degrees * Math.PI / 180.0);
        Quaternion Rotation = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, Radians);
        return Rotate(Field, Rotation);
    }

    public float Eval(Vector3 EvalPoint)
    {
        var Stack = new float[StackSize];
        long ProgramCounter = 0;
        long StackPointer = 0;
        long Brush = 0;

        var ReadSymbol = () =>
        {
            return Words[ProgramCounter++].Symbol;
        };

        var ReadValue = () =>
        {
            return Words[ProgramCounter++].Value;
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

        while (ProgramCounter < WordCount)
        {
            switch (ReadSymbol())
            {
                case Opcode.Sphere:
                {
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
                    float Radius = ReadValue();
                    float Dist = Point.Length() - Radius;
                    StackPush(Dist);
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Box:
                {
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
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
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
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
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
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

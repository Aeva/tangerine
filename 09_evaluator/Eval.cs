using System;
using System.Numerics;
using System.Diagnostics;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
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
public readonly struct ProgramWord
{
    [System.Runtime.InteropServices.FieldOffset(0)]
    public readonly Opcode Symbol;

    [System.Runtime.InteropServices.FieldOffset(0)]
    public readonly float Value;

    public ProgramWord(Opcode InSymbol)
    {
        Unsafe.SkipInit(out Value);
        Symbol = InSymbol;
    }

    public ProgramWord(float InValue)
    {
        Unsafe.SkipInit(out Symbol);
        Value = InValue;
    }

    public static implicit operator ProgramWord(Opcode InSymbol) => new ProgramWord(InSymbol);
    public static implicit operator ProgramWord(float InValue) => new ProgramWord(InValue);
}


public readonly struct Transform
{
    private readonly Quaternion Rotation;
    private readonly Vector3 Translation;
    private readonly float Scalation;

    public Transform()
    {
        Rotation = Quaternion.Identity;
        Translation = Vector3.Zero;
        Scalation = 1.0f;
    }

    public Transform(
        Quaternion InRotation,
        Vector3 InTranslation,
        float InScalation)
    {
        Rotation = InRotation;
        Translation = InTranslation;
        Scalation = InScalation;
    }

    public Transform Move(Vector3 OffsetBy)
    {
        return new Transform(Rotation, Translation + OffsetBy, Scalation);
    }

    public Transform Rotate(Quaternion RotateBy)
    {
        return new Transform(RotateBy * Rotation, Vector3.Transform(Translation, RotateBy), Scalation);
    }

    public Transform Scale(float ScaleBy)
    {
        return new Transform(Rotation, Translation * ScaleBy, Scalation * ScaleBy);
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


public readonly struct ProgramBuffer
{
    public readonly List<ProgramWord> Words;
    public readonly List<Transform> BrushTransforms;
    public readonly int StackSize;

    private ProgramBuffer(params ProgramWord[] InitialWords)
    {
        Words = new List<ProgramWord>(InitialWords);
        BrushTransforms = new List<Transform>(1);
        BrushTransforms.Add(new Transform());
        StackSize = 1;
    }

    private ProgramBuffer(ProgramBuffer CopyTarget, Func<Transform, Transform> TransformFn)
    {
        Words = new List<ProgramWord>(CopyTarget.Words.Count);
        Words.AddRange(CopyTarget.Words);
        BrushTransforms = new List<Transform>(CopyTarget.BrushTransforms.Count);
        foreach (Transform BrushTransform in CopyTarget.BrushTransforms)
        {
            BrushTransforms.Add(TransformFn(BrushTransform));
        }
        StackSize = CopyTarget.StackSize;
    }

    private ProgramBuffer(ProgramBuffer CopyLHS, ProgramBuffer CopyRHS, params ProgramWord[] Append)
    {
        Words = new List<ProgramWord>(CopyLHS.Words.Count + CopyLHS.Words.Count + Append.Length);
        Words.AddRange(CopyLHS.Words);
        Words.AddRange(CopyRHS.Words);
        Words.AddRange(Append);
        BrushTransforms = new List<Transform>(CopyLHS.BrushTransforms.Count + CopyRHS.BrushTransforms.Count);
        BrushTransforms.AddRange(CopyLHS.BrushTransforms);
        BrushTransforms.AddRange(CopyRHS.BrushTransforms);
        StackSize = Math.Max(CopyLHS.StackSize, CopyRHS.StackSize + 1);
    }

    public static ProgramBuffer Sphere(float Diameter)
    {
        return new ProgramBuffer(Opcode.Sphere, Diameter * 0.5f);
    }

    public static ProgramBuffer Ellipsoid(float DiameterX, float DiameterY, float DiameterZ)
    {
        return new ProgramBuffer(Opcode.Ellipsoid, DiameterX * 0.5f, DiameterY * 0.5f, DiameterZ * 0.5f);
    }

    public static ProgramBuffer Box(float SpanX, float SpanY, float SpanZ)
    {
        return new ProgramBuffer(Opcode.Box, SpanX * 0.5f, SpanY * 0.5f, SpanZ * 0.5f);
    }

    public static ProgramBuffer Cube(float Span)
    {
        return Box(Span, Span, Span);
    }

    public static ProgramBuffer Torus(float MajorDiameter, float MinorDiameter)
    {
        return new ProgramBuffer(Opcode.Torus, MajorDiameter * 0.5f, MinorDiameter * 0.5f);
    }

    public static ProgramBuffer Cylinder(float Diameter, float Height)
    {
        return new ProgramBuffer(Opcode.Cylinder, Diameter * 0.5f, Height * 0.5f);
    }

    public static ProgramBuffer Plane(float NormalX, float NormalY, float NormalZ)
    {
        return new ProgramBuffer(Opcode.Plane, NormalX, NormalY, NormalZ);
    }

    public static ProgramBuffer Union(ProgramBuffer LHS, ProgramBuffer RHS)
    {
        return new ProgramBuffer(LHS, RHS, Opcode.Union);
    }

    public static ProgramBuffer Inter(ProgramBuffer LHS, ProgramBuffer RHS)
    {
        return new ProgramBuffer(LHS, RHS, Opcode.Inter);
    }

    public static ProgramBuffer Diff(ProgramBuffer LHS, ProgramBuffer RHS)
    {
        return new ProgramBuffer(LHS, RHS, Opcode.Diff);
    }

    public static ProgramBuffer Union(ProgramBuffer LHS, ProgramBuffer RHS, float Threshold)
    {
        return new ProgramBuffer(LHS, RHS, Opcode.BlendUnion, Threshold);
    }

    public static ProgramBuffer Inter(ProgramBuffer LHS, ProgramBuffer RHS, float Threshold)
    {
        return new ProgramBuffer(LHS, RHS, Opcode.BlendInter, Threshold);
    }

    public static ProgramBuffer Diff(ProgramBuffer LHS, ProgramBuffer RHS, float Threshold)
    {
        return new ProgramBuffer(LHS, RHS, Opcode.BlendDiff, Threshold);
    }

    public static ProgramBuffer Move(ProgramBuffer Field, Vector3 Offset)
    {
        return new ProgramBuffer(Field, (Transform BrushTransform) =>
        {
            return BrushTransform.Move(Offset);
        });
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
        return new ProgramBuffer(Field, (Transform BrushTransform) =>
        {
            return BrushTransform.Rotate(Rotation);
        });
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public float ReadValue(ref int ProgramCounter)
    {
        return Words[ProgramCounter++].Value;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector3 ReadVec3(ref int ProgramCounter)
    {
        Vector3 Vec;
        Vec.X = Words[ProgramCounter++].Value;
        Vec.Y = Words[ProgramCounter++].Value;
        Vec.Z = Words[ProgramCounter++].Value;
        return Vec;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public float Eval(Vector3 EvalPoint)
    {
        Span<float> Stack = stackalloc float[StackSize];

        int ProgramCounter = 0;
        int StackPointer = 0;
        int Brush = 0;

        while (ProgramCounter < Words.Count)
        {
            switch (Words[ProgramCounter++].Symbol)
            {
                case Opcode.Sphere:
                {
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
                    float Radius = ReadValue(ref ProgramCounter);
                    float Dist = Point.Length() - Radius;
                    Stack[StackPointer++] = Dist;
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Box:
                {
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
                    Vector3 Extent = ReadVec3(ref ProgramCounter);
                    Vector3 A = Vector3.Abs(Point) - Extent;
                    float Dist = Vector3.Max(A, Vector3.Zero).Length() + Math.Min(Math.Max(Math.Max(A.X, A.Y), A.Z), 0.0f);
                    Stack[StackPointer++] = Dist;
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Cylinder:
                {
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
                    float Radius = ReadValue(ref ProgramCounter);
                    float Extent = ReadValue(ref ProgramCounter);
                    Vector2 D;
                    D.X = Point.X;
                    D.Y = Point.Y;
                    D.X = D.Length() - Radius;
                    D.Y = Math.Abs(Point.Z) - Extent;
                    float Dist = Math.Min(Math.Max(D.X, D.Y), 0.0f) + Vector2.Max(D, Vector2.Zero).Length();
                    Stack[StackPointer++] = Dist;
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Plane:
                {
                    Vector3 Point = BrushTransforms[Brush++].ApplyInv(EvalPoint);
                    Vector3 Normal = ReadVec3(ref ProgramCounter);
                    float Dist = Vector3.Dot(Point, Normal);
                    Stack[StackPointer++] = Dist;
                    Point = EvalPoint;
                    break;
                }

                case Opcode.Union:
                {
                    float RHS = Stack[--StackPointer];
                    float LHS = Stack[--StackPointer];
                    float Dist = Math.Min(LHS, RHS);
                    Stack[StackPointer++] = Dist;
                    break;
                }

                case Opcode.Inter:
                {
                    float RHS = Stack[--StackPointer];
                    float LHS = Stack[--StackPointer];
                    float Dist = Math.Max(LHS, RHS);
                    Stack[StackPointer++] = Dist;
                    break;
                }

                case Opcode.Diff:
                {
                    float RHS = Stack[--StackPointer];
                    float LHS = Stack[--StackPointer];
                    float Dist = Math.Max(LHS, -RHS);
                    Stack[StackPointer++] = Dist;
                    break;
                }

                case Opcode.BlendUnion:
                {
                    float RHS = Stack[--StackPointer];
                    float LHS = Stack[--StackPointer];
                    float Threshold = ReadValue(ref ProgramCounter);
                    float H = Math.Max(Threshold - Math.Abs(LHS - RHS), 0.0f);
                    float Dist = Math.Min(LHS, RHS) - H * H * 0.25f / Threshold;
                    Stack[StackPointer++] = Dist;
                    break;
                }

                case Opcode.BlendInter:
                {
                    float RHS = Stack[--StackPointer];
                    float LHS = Stack[--StackPointer];
                    float Threshold = ReadValue(ref ProgramCounter);
                    float H = Math.Max(Threshold - Math.Abs(LHS - RHS), 0.0f);
                    float Dist = Math.Max(LHS, RHS) + H * H * 0.25f / Threshold;
                    Stack[StackPointer++] = Dist;
                    break;
                }

                case Opcode.BlendDiff:
                {
                    float RHS = Stack[--StackPointer];
                    float LHS = Stack[--StackPointer];
                    float Threshold = ReadValue(ref ProgramCounter);
                    float H = Math.Max(Threshold - Math.Abs(LHS + RHS), 0.0f);
                    float Dist = Math.Max(LHS, -RHS) + H * H * 0.25f / Threshold;
                    Stack[StackPointer++] = Dist;
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
        float Result = Stack[--StackPointer];
        Debug.Assert(StackPointer == 0);
        return Result;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
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

using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

using Color = Microsoft.Xna.Framework.Color;
using Vector3 = System.Numerics.Vector3;
using Vector4 = System.Numerics.Vector4;
using Matrix4x4 = System.Numerics.Matrix4x4;

using static Microsoft.Xna.Framework.MathHelper;
using System.IO;
using PerfCounter = Perf.PerfCounter;

using static Evaluator.ProgramBuffer;

namespace Experiment;


public class Experiment : Game
{
    // This determines the maximum number of surfels that will be rendered
    // every frame, which effectively controls the point density as well as
    // Convergence time.  If set too high, frame drops can occur.  If set
    // too low, the image will look chunky.
    private int MaxSurfels  = 50_000;

    // This is the target number of new surfels to trace every generation.
    // This tracing is performed asynchronously, but the tracing rate should
    // roughly match the amount of surfels that can be traced in one vsync
    // interval to ensure a good screen space distribution and fill rate.
    // Setting this a bit higher than expected may sometimes produce better
    // results, however.
    private int TracingRate = 1_800;

    // View space distance.
    private float SplatDepth = 0.01f;

    // Vertex counts per loop.
    private int[] SplatRings = {1, 5, 20};

    private int WindowWidth = 2256;
    private int WindowHeight = 1504;
    private bool FullScreen = true;
    private bool VSync = true;

    // Units per second
    private float Acceleration = 15.0f;
    private float TopSpeed = 60.0f;

    // Degrees per second
    private float TurnSpeed = 30.0f;

    // Degrees
    private float CurrentHeading = 0.0f;

    // Units per second
    private Vector3 LinearVelocity = Vector3.Zero;

    private double FieldOfView = 60;
    private double NearPlane = 0.001;

    private int Paused = 0;
    private long LastFrameTicks = 0;
    private double RunTimeMs = 0.0;

    private readonly Evaluator.ProgramBuffer Model;

    private GraphicsDeviceManager _graphics;
    private Effect InstancedBasicEffect;
    private VertexBufferBinding[] SplatBindings;

    private VertexBuffer VoronoiVertexBuffer;
    private IndexBuffer VoronoiIndexBuffer;
    private int VoronoiTriangleCount = 0;
    private int VoronoiVertexCount = 0;
    private int VoronoiIndexCount = 0;

    private VertexDeclaration PositionDesc;
    private VertexBuffer PositionBuffer;
    private Vector4[] Positions;

    private VertexDeclaration NormalDesc;
    private VertexBuffer NormalBuffer;
    private Vector4[] Normals;

    private VertexDeclaration ColorDesc;
    private VertexBuffer ColorBuffer;
    private Color[] Colors;

    private int LiveSurfels = 0;
    private int WriteCursor = 0;

    private Matrix4x4 LocalToWorld;
    private Matrix4x4 WorldToView;
    private Matrix4x4 ViewToClip;

    private Matrix4x4 ClipToView;

    private float AspectRatio;
    private int FrustaCountX;
    private int FrustaCountY;
    private float FineDiameter; // Fine grain diameter.
    private float CoarseDiameter; // Coarse grain diameter.
    private float GrainAlpha = 0.25f;

    private PerfCounter FrameRate = new PerfCounter();
    private PerfCounter SplatCopyCount = new PerfCounter();
    private PerfCounter SplatCopyTime = new PerfCounter();
    private long LastPerfLog = 0;

    private CancellationTokenSource CancelSource = new CancellationTokenSource();

    private ConcurrentQueue<List<(Vector4 Position, Vector4 Normal, Color Color)>> PendingSurfels;

    private Vector3[] LightPoints = new Vector3[3];
    private Vector3[] LightColors = new Vector3[3];
    private Vector3 Eye = new Vector3(5.0f, -8.0f, 2.0f);
    private Vector3 EyeDir = new Vector3(0.0f, 1.0f, 0.0f);

    public Experiment()
    {
        _graphics = new GraphicsDeviceManager(this);
        Content.RootDirectory = "Content";
        IsMouseVisible = true;
        IsFixedTimeStep = false;

        _graphics.HardwareModeSwitch = false;
        _graphics.GraphicsProfile = GraphicsProfile.HiDef;
        _graphics.SynchronizeWithVerticalRetrace = VSync;

        SplatBindings = new VertexBufferBinding[4];

        if (FullScreen)
        {
            _graphics.PreferredBackBufferWidth = GraphicsAdapter.DefaultAdapter.CurrentDisplayMode.Width;
            _graphics.PreferredBackBufferHeight = GraphicsAdapter.DefaultAdapter.CurrentDisplayMode.Height;
            _graphics.IsFullScreen = true;
            IsMouseVisible = false;
        }
        else
        {
            _graphics.PreferredBackBufferWidth = WindowWidth;
            _graphics.PreferredBackBufferHeight = WindowHeight;
            _graphics.IsFullScreen = false;
            IsMouseVisible = true;
        }

        double ScreenW = _graphics.PreferredBackBufferWidth;
        double ScreenH = _graphics.PreferredBackBufferHeight;
        AspectRatio = (float)(ScreenW / ScreenH);

        double ScreenArea = ScreenW * ScreenH;

        double MinSquareArea = ScreenArea / (double)MaxSurfels;
        double MinSquareEdge = Math.Sqrt(MinSquareArea);
        FineDiameter = (float)(MinSquareEdge / ScreenH * Math.Sqrt(2.0));

        double MaxSquareArea = ScreenArea / (double)TracingRate;
        double MaxSquareEdge = Math.Sqrt(MaxSquareArea);
        double CoarseFudge = 0.5; // TODO maybe the shading rate should be derrived from the grain size instead of vice versa?
        CoarseDiameter = (float)(MaxSquareEdge / ScreenH * Math.Sqrt(2.0) * CoarseFudge);

        FrustaCountX = (int)Math.Floor((ScreenW / MaxSquareEdge));
        FrustaCountY = (int)Math.Floor((ScreenH / MaxSquareEdge));
        TracingRate = FrustaCountX * FrustaCountY;

        _graphics.ApplyChanges();

        Positions = new Vector4[MaxSurfels];
        Normals = new Vector4[MaxSurfels];
        Colors = new Color[MaxSurfels];

        LightColors[0] = new Vector3(1.0f, 0.0f, 0.0f);
        LightColors[1] = new Vector3(0.0f, 1.0f, 0.0f);
        LightColors[2] = new Vector3(0.0f, 0.0f, 1.0f);

        PendingSurfels = new ConcurrentQueue<List<(Vector4 Position, Vector4 Normal, Color Color)>>();

        {
#if true
            var BasicThing =
                Diff(
                    Inter(
                        Cube(4.0f),
                        Sphere(5.5f)),
                    Union(
                        Cylinder(3.0f, 5.0f),
                        Union(
                            RotateX(Cylinder(3.0f, 5.0f), 90.0f),
                            RotateY(Cylinder(3.0f, 5.0f), 90.0f))));
            BasicThing = MoveZ(BasicThing, 2.0f);
#else
            var BasicThing =
                MoveZ(Box(4.0f, 10.0f, 5.0f), 2.5f);
#endif
            var Ground =
                Plane(0.0f, 0.0f, 1.0f);

            Model = Union(Ground, BasicThing);
        }

        {
            LocalToWorld = Matrix4x4.Identity;
            WorldToView = Matrix4x4.CreateLookTo(
                Eye,
                EyeDir,
                new Vector3(0, 0, 1));
            InfinitePerspective(out ViewToClip);

            ClipToView = Matrix4x4.Identity;
            Matrix4x4.Invert(ViewToClip, out ClipToView);

#if false
            // UV.x 0 to 1 maps to worldspace -X to +X
            // UV.y 0 to 1 maps to worldspace -Z to +Z
            Vector2 UV = new Vector2(0.0f, 0.0f);
            //Vector4 Fnord = new Vector4(0, 0, (float)NearPlane, 1);
            //Vector4 Meep = Vector4.Transform(Fnord, ViewToClip);
            Vector4 ClipTarget = new Vector4(UV.X * 2.0f - 1.0f, UV.Y * 2.0f - 1.0f, -1, 1);
            Console.WriteLine($"ClipTarget: {ClipTarget}");

            Vector4 ViewTarget = Vector4.Transform(ClipTarget, ClipToView);
            ViewTarget /= ViewTarget.W;
            Console.WriteLine($"ViewTarget: {ViewTarget}");

            Vector4 WorldTarget = Vector4.Transform(ViewTarget, ViewToWorld);
            WorldTarget /= WorldTarget.W;
            Console.WriteLine($"WorldTarget: {WorldTarget}");

            Vector3 ViewRayDir = new Vector3(ViewTarget.X, ViewTarget.Y, ViewTarget.Z);
            ViewRayDir = Vector3.Normalize(ViewRayDir);
            Console.WriteLine($"ViewRayDir: {ViewRayDir}");

            Vector3 WorldRayDir = new Vector3(WorldTarget.X, WorldTarget.Y, WorldTarget.Z);
            WorldRayDir = Vector3.Normalize(WorldRayDir);
            Console.WriteLine($"WorldRayDir: {WorldRayDir}");
#endif
        }

        PositionDesc = new VertexDeclaration(
            new VertexElement(0, VertexElementFormat.Vector4, VertexElementUsage.TextureCoordinate, 0));

        NormalDesc = new VertexDeclaration(
            new VertexElement(0, VertexElementFormat.Vector4, VertexElementUsage.Normal, 0));

        ColorDesc = new VertexDeclaration(
            new VertexElement(0, VertexElementFormat.Color, VertexElementUsage.Color, 0));
    }

    protected override void Initialize()
    {
        Window.Title = "Star Machine";
        base.Initialize();
        LastFrameTicks = DateTime.UtcNow.Ticks;
    }

    private float EvalModel(Vector3 Point)
    {
        float Span = 10.0f;
        Point.X -= Span * (float)Math.Round(Point.X / Span);
        Point.Y -= Span * (float)Math.Round(Point.Y / Span);

        return Model.Eval(Point);
    }

    private Vector3 Gradient(Vector3 Point)
    {
        float Span = 10.0f;
        Point.X -= Span * (float)Math.Round(Point.X / Span);
        Point.Y -= Span * (float)Math.Round(Point.Y / Span);

        return Model.Gradient(Point);
    }

    private (bool, Vector3) Trace(Vector3 Start, Vector3 Stop)
    {
        Vector3 Point = Start;
        Vector3 Dir = Vector3.Normalize(Stop - Start);
        float Travel = 0.0f;
        for (int Iteration = 0; Iteration < 1000; ++Iteration)
        {
            float Dist = EvalModel(Point);
            if (Dist <= 0.001f)
            {
                return (true, Point);
            }
            else if (Dist >= 100.0f)
            {
                break;
            }
            else
            {
                Travel += Dist;
                Point = Dir * Travel + Start;
            }
        }
        return (false, Point);
    }

    private (bool, float) TravelTrace(Vector3 Start, Vector3 Dir, float MaxTravel, float Margin)
    {
        MaxTravel += Margin;
        Vector3 Point = Start;
        Dir = Vector3.Normalize(Dir);
        float Travel = 0.0f;
        for (int Iteration = 0; Iteration < 1000 && Travel < MaxTravel; ++Iteration)
        {
            float Dist = EvalModel(Point) - Margin;
            if (Dist <= 0.001f)
            {
                return (true, Math.Max(Travel - Margin, 0.0f));
            }
            else
            {
                Travel += Dist;
                Point = Dir * Travel + Start;
            }
        }
        return (false, Math.Max(MaxTravel - Margin, 0.0f));
    }

    private (bool, float) TravelTrace(Vector3 Start, Vector2 FlatDir, float MaxTravel, float Margin)
    {
        Vector3 Dir;
        Dir.X = FlatDir.X;
        Dir.Y = FlatDir.Y;
        Dir.Z = 0.0f;
        return TravelTrace(Start, Dir, MaxTravel, Margin);
    }

    private float LightTrace(Vector3 Start, Vector3 Stop, float LightSize)
    {
        Vector3 Dir = Stop - Start;
        float Travel = 0.0f;
        float MaxTravel = Dir.Length();
        Dir /= MaxTravel;

        float Res = 1.0f;

        for (int Iteration = 0; Iteration < 100; ++Iteration)
        {
            float Dist = EvalModel(Dir * Travel + Start);
            Res = Math.Min(Res, Dist / (LightSize * Travel));
            Travel += Math.Max(Dist, 0.005f);

            if (Res < -1.0 || Travel >= MaxTravel)
            {
                break;
            }
        }

        Res = Math.Max(Res, -1.0f);
        return 0.25f * (1.0f + Res) * (1.0f + Res) * (2.0f - Res);
    }

    private void PopulateSplats(ParallelOptions parallelOptions)
    {
        Matrix4x4 ViewToWorld = Matrix4x4.Identity;
        Matrix4x4 WorldToLocal = Matrix4x4.Identity;

        Matrix4x4.Invert(WorldToView, out ViewToWorld);
        Matrix4x4.Invert(LocalToWorld, out WorldToLocal);

        var CeilDivide = (int Numerator, int Denominator) =>
        {
            return (Numerator + Denominator - 1) / Denominator;
        };

        int CurrentTracingRate = TracingRate;

        int SliceSize = CurrentTracingRate;
        int SliceCount = 1;

        if (CurrentTracingRate > parallelOptions.MaxDegreeOfParallelism)
        {
            int MaximumBatchSize = 16;
            SliceSize = Math.Min(CeilDivide(CurrentTracingRate, parallelOptions.MaxDegreeOfParallelism), MaximumBatchSize);
            SliceCount = CeilDivide(CurrentTracingRate, SliceSize);
            SliceSize = CeilDivide(CurrentTracingRate, SliceCount);
        }

        Color MissColor = new Color(0.2f, 0.2f, 0.2f, 1.0f);

        Parallel.For(0, SliceCount, parallelOptions, (SliceIndex) =>
        {
            int SliceStart = SliceIndex * SliceSize;
            int SliceStop = Math.Min(CurrentTracingRate, SliceStart + SliceSize);
            int Range = SliceStop - SliceStart;

            var NewSurfels = new List<(Vector4 Position, Vector4 Normal, Color Color)>(Range);

            var SplatRNG = new Random();

            for (int BatchIndex = 0; BatchIndex < Range; ++BatchIndex)
            {
                int Cursor = SliceStart + BatchIndex;

                Vector3 RayDir;
                {
#if true
                    float JitterX = (float)SplatRNG.Next(-1000, 1000) / 1000.0f * 0.5f;
                    float JitterY = (float)SplatRNG.Next(-1000, 1000) / 1000.0f * 0.5f;
#else
                    float JitterX = 0.0f;
                    float JitterY = 0.0f;
#endif

                    float FrustumX = (float)(Cursor % FrustaCountX) + 0.5f + JitterX;
                    float FrustumY = (float)(Cursor / FrustaCountX) + 0.5f + JitterY;

                    float Overscan = Lerp(1.1f, 1.5f, GrainAlpha);

                    Vector4 ClipTarget;
                    ClipTarget.X = (FrustumX / (float)FrustaCountX * 2.0f - 1.0f) * Overscan;
                    ClipTarget.Y = (FrustumY / (float)FrustaCountY * 2.0f - 1.0f) * Overscan;
                    ClipTarget.Z = -1;
                    ClipTarget.W = 1;

                    Vector4 ViewTarget = Vector4.Transform(ClipTarget, ClipToView);
                    ViewTarget /= ViewTarget.W;

                    Vector3 ViewRayDir;
                    ViewRayDir.X = ViewTarget.X;
                    ViewRayDir.Y = ViewTarget.Y;
                    ViewRayDir.Z = ViewTarget.Z;
                    ViewRayDir = Vector3.Normalize(ViewRayDir);

                    RayDir = Vector3.TransformNormal(ViewRayDir, ViewToWorld);
                }

                var Start = Eye;
                var Stop = RayDir * 100_000.0f + Eye;

                if (Start != Stop)
                {
                    (bool Hit, Vector3 Position) = Trace(Start, Stop);
                    if (Hit)
                    {
                        var Normal = Gradient(Position);

                        var SplatColor = new Vector3(0.0f, 0.0f, 0.0f);
                        for (int LightIndex = 0; LightIndex < LightPoints.Length; ++LightIndex)
                        {
                            var LightPoint = Vector3.Transform(LightPoints[LightIndex], WorldToLocal);
                            var LightColor = LightColors[LightIndex];

                            var Offset = Normal * 0.01f + Position;
                            float Visibility = LightTrace(Normal * 0.01f + Position, LightPoint, 0.1f);

                            if (Visibility > 0.0f)
                            {
                                var LightRay = Vector3.Normalize(LightPoint - Position);

                                float Luminence = Math.Max(Vector3.Dot(LightRay, Normal), 0.0f) * Visibility;

                                SplatColor += LightColor * Luminence;
                            }
                        }
                        NewSurfels.Add((new Vector4(Position, 1.0f), new Vector4(Normal, 1.0f), new Color(SplatColor)));
                        continue;
                    }
                }
                {
                    //NewSurfels.Add((new Vector4(Stop, 1.0f), new Vector4(-RayDir, 1.0f), Color.CornflowerBlue));
                    NewSurfels.Add((new Vector4(Stop, 1.0f), new Vector4(-RayDir, 1.0f), MissColor));
                    continue;
                }
            }

            if (NewSurfels.Count > 0)
            {
                PendingSurfels.Enqueue(NewSurfels);
            }
        });
    }

    private void InfinitePerspective(out Matrix4x4 Result)
    {
        // equiv to Math.Tan(Math.PI / 180 * FieldOfView / 2) * NearPlane
        double View = Math.Tan(Math.PI / 360 * FieldOfView) * NearPlane;
        Result = Matrix4x4.Identity;
        Result[0, 0] = (float)(NearPlane / View / (double)AspectRatio);
        Result[1, 1] = (float)(NearPlane / View);
        Result[2, 2] = -1.0f;
        Result[3, 3] = 0.0f;
        Result[2, 3] = -1.0f;
        Result[3, 2] = (float)(-2.0f * NearPlane);
    }

    protected override void LoadContent()
    {
        {
            byte[] ShaderBytes = File.ReadAllBytes("Shaders/InstancedSplats.ogl.mgfxo");
            InstancedBasicEffect = new Effect(GraphicsDevice, ShaderBytes);
            InstancedBasicEffect.CurrentTechnique = InstancedBasicEffect.Techniques[0];
        }

        {
            var Offsets = new int[SplatRings.Length];
            VoronoiVertexCount = SplatRings.Sum();

            var DiscPoint = (float Radius, float Degrees) =>
            {
                float Angle = ToRadians(Degrees);
                var Vertex = new Vector3(
                    (float)System.Math.Sin(Angle) * Radius,
                    (float)System.Math.Cos(Angle) * Radius,
                    0.0f);
                Vertex.Z = -Vector3.Dot(Vertex, Vertex);
                return Vertex;
            };

            {
                var ParaboloidVertices = new VertexPosition[VoronoiVertexCount];
                int Offset = 0;
                int Ring = 0;

                foreach (int VertexCount in SplatRings)
                {
                    if (VertexCount == 1)
                    {
                        ParaboloidVertices[Offset].Position = new Vector3(0.0f, 0.0f, 0.0f);
                    }
                    else
                    {
                        Debug.Assert(VertexCount >= 3);
                        int RingNudge = SplatRings[0] == 1 ? 0 : 1;
                        float Radius = (float)(Ring + RingNudge) / (float)(SplatRings.Length - 1  + RingNudge);
                        for (int Index = 0; Index < VertexCount; ++Index)
                        {
                            float Angle = (float)(Index) / (float)(VertexCount) * 360.0f;
                            ParaboloidVertices[Offset + Index].Position = DiscPoint(Radius, Angle);
                        }
                    }
                    Offsets[Ring] = Offset;
                    ++Ring;
                    Offset += VertexCount;
                }


                VoronoiVertexBuffer = new VertexBuffer(
                    GraphicsDevice,
                    typeof(VertexPosition),
                    VoronoiVertexCount,
                    BufferUsage.WriteOnly);
                VoronoiVertexBuffer.SetData<VertexPosition>(ParaboloidVertices);
            }

            VoronoiIndexCount = 0;
            {
                var DecodeRingIndex = (int Index, int Ring) =>
                {
                    int Offset = Offsets[Ring];
                    int Range = SplatRings[Ring];
                    return (((Index - Offset) % Range) + Offset);
                };

                var Loops = new List<List<short>>();
                int RingA;
                for (RingA = 0; RingA < SplatRings.Length - 1; ++RingA)
                {
                    var Strip = new List<short>();
                    int RingB = RingA + 1;
                    int Dialation = SplatRings[RingA] * SplatRings[RingB];
                    int StrideA = Dialation / SplatRings[RingA];
                    int StrideB = Dialation / SplatRings[RingB];
                    int OffsetA = Offsets[RingA];
                    int OffsetB = Offsets[RingB];
                    (int A, int B) Last = (OffsetA, OffsetB);

                    var RecordTriangle = ((int A, int B) LHS, (int A, int B) RHS) =>
                    {
                        int Wrote = 2;
                        Strip.Add((short)LHS.B);
                        if (LHS.B != RHS.B)
                        {
                            ++Wrote;
                            Strip.Add((short)RHS.B);
                        }

                        Strip.Add((short)RHS.A);
                        if (LHS.A != RHS.A)
                        {
                            ++Wrote;
                            Strip.Add((short)LHS.A);
                        }
                        Debug.Assert(Wrote == 3);
                    };

                    var RecordLine = ((int A, int B) Next) =>
                    {
                        int A1 = DecodeRingIndex(Last.A, RingA);
                        int A2 = DecodeRingIndex(Next.A, RingA);
                        int B1 = DecodeRingIndex(Last.B, RingB);
                        int B2 = DecodeRingIndex(Next.B, RingB);

                        bool MatchA = (A1 == A2);
                        bool MatchB = (B1 == B2);
                        if (MatchA && MatchB)
                        {
                            // Matched a repeat.
                            return;
                        }
                        else if (MatchA == MatchB)
                        {
                            // Matched a quad.
                            RecordTriangle((A1, B1), (A2, B1));
                            RecordTriangle((A2, B1), (A2, B2));
                            Last = Next;
                        }
                        else
                        {
                            // Matched a triangle.
                            RecordTriangle((A1, B1), (A2, B2));
                            Last = Next;
                        }
                    };

                    for (int Cursor = 1; Cursor < Dialation; ++Cursor)
                    {
                        (int A, int B) Next = (Cursor / StrideA + OffsetA, Cursor / StrideB + OffsetB);
                        RecordLine(Next);
                    }
                    {
                        (int A, int B) Next = (OffsetA + SplatRings[RingA], OffsetB + SplatRings[RingB]);
                        RecordLine(Next);
                    }

                    VoronoiIndexCount += Strip.Count;
                    Loops.Add(Strip);
                }

                VoronoiTriangleCount = VoronoiIndexCount / 3;
                var ParaboloidIndices = new short[VoronoiIndexCount];
                {
                    int Cursor = 0;
                    foreach (var Strip in Loops)
                    {
                        foreach (short Index in Strip)
                        {
                            ParaboloidIndices[Cursor++] = Index;
                        }
                    }
                }

                VoronoiIndexBuffer = new IndexBuffer(
                    GraphicsDevice,
                    IndexElementSize.SixteenBits,
                    sizeof(short) * VoronoiIndexCount,
                    BufferUsage.WriteOnly);
                VoronoiIndexBuffer.SetData<short>(ParaboloidIndices);
            }
        }

        {
            PositionBuffer = new VertexBuffer(GraphicsDevice, PositionDesc, MaxSurfels, BufferUsage.WriteOnly);
            NormalBuffer = new VertexBuffer(GraphicsDevice, NormalDesc, MaxSurfels, BufferUsage.WriteOnly);
            ColorBuffer = new VertexBuffer(GraphicsDevice, ColorDesc, MaxSurfels, BufferUsage.WriteOnly);
        }

        {
            SplatBindings[0] = new VertexBufferBinding(VoronoiVertexBuffer, 0, 0);
            SplatBindings[1] = new VertexBufferBinding(PositionBuffer, 0, 1);
            SplatBindings[2] = new VertexBufferBinding(NormalBuffer, 0, 1);
            SplatBindings[3] = new VertexBufferBinding(ColorBuffer, 0, 1);
        }

        {
            var parallelOptions = new ParallelOptions();
            parallelOptions.CancellationToken = CancelSource.Token;
            parallelOptions.MaxDegreeOfParallelism = Math.Max(Environment.ProcessorCount - 2, 1);

            Task.Run(() => {
                while(!parallelOptions.CancellationToken.IsCancellationRequested)
                {
                    if (PendingSurfels.Count == 0)
                    {
                        try
                        {
                            PopulateSplats(parallelOptions);
                        }
                        catch (OperationCanceledException)
                        {
                            return;
                        }
                    }
                    else
                    {
                        //Thread.Sleep(1);
                        Thread.Yield();
                    }
                }
            }, CancelSource.Token);
        }

        FrameRate.Reset();
        SplatCopyCount.Reset();
        SplatCopyTime.Reset();
    }

    protected override void Update(GameTime gameTime)
    {
        double ElapsedTimeMs = 0.0f;
        long CurrentFrameTicks = DateTime.UtcNow.Ticks;
        {
            long ElapsedTicks = CurrentFrameTicks - LastFrameTicks;
            LastFrameTicks = CurrentFrameTicks;

            if (Paused == 0)
            {
                ElapsedTimeMs = (double)ElapsedTicks / (double)TimeSpan.TicksPerMillisecond;
                RunTimeMs += ElapsedTimeMs;
            }
        }

        if (GamePad.GetState(PlayerIndex.One).Buttons.Back == ButtonState.Pressed || Keyboard.GetState().IsKeyDown(Keys.Escape))
        {
            CancelSource.Cancel();
            Exit();
        }

        if ((Paused % 2) == 0 && Keyboard.GetState().IsKeyDown(Keys.P))
        {
            Paused = (Paused + 1) % 4;
        }
        else if ((Paused % 2) == 1 && Keyboard.GetState().IsKeyUp(Keys.P))
        {
            Paused = (Paused + 1) % 4;
        }

        FrameRate.LogFrame();

        if (Paused == 0)
        {
            var FindLightPosition = (double Speed, double Phase) =>
            {
                double T = RunTimeMs / 5000.0;
                double P = 2.0 * Math.PI * Phase;
                float S = (float)Math.Sin(T * Speed + P);
                float C = (float)Math.Cos(T * Speed + P);
                return Eye + new Vector3(S * 20.0f, C * 20.0f, 15.0f);
            };

            LightPoints[0] = FindLightPosition(1.0, 0.0 / 3.0);
            LightPoints[1] = FindLightPosition(2.0, 1.0 / 3.0);
            LightPoints[2] = FindLightPosition(-4.0, 2.0 / 3.0);
#if false
            {
                //float T = (float)(RunTimeMs / -500.0 * Math.PI);
                float T = (float)(RunTimeMs / -10000.0 * Math.PI);
                //float T = (float)(RunTimeMs / -1000000.0 * Math.PI);
                float S = (float)Math.Sin(T);
                float C = (float)Math.Cos(T);
                Eye = new Vector3(S * 8.0f, C * 8.0f, 2.0f);
            }
#endif
        }

        {
            float Seconds = (float)(ElapsedTimeMs / 1000.0);

            float Turn = 0.0f;
            if (Keyboard.GetState().IsKeyDown(Keys.Left))
            {
                Turn -= TurnSpeed * Seconds;
            }
            if (Keyboard.GetState().IsKeyDown(Keys.Right))
            {
                Turn += TurnSpeed * Seconds;
            }
            if (Math.Abs(Turn) > 0.001)
            {
                CurrentHeading = (CurrentHeading + Turn) % 360.0f;
                float Radians = (float)(Math.PI / 180.0) * CurrentHeading;
                EyeDir.X = (float)Math.Sin(Radians);
                EyeDir.Y = (float)Math.Cos(Radians);
                EyeDir.Z = 0.0f;
                EyeDir = Vector3.Normalize(EyeDir);
            }

            if (Keyboard.GetState().IsKeyDown(Keys.Up))
            {
                LinearVelocity += EyeDir * Acceleration * Seconds;
            }
            else
            {
                LinearVelocity *= 0.99f;
            }
            if (Keyboard.GetState().IsKeyDown(Keys.Down))
            {
                float Magnitude = LinearVelocity.Length();
                if (Magnitude > 0.0f)
                {
                    LinearVelocity = (LinearVelocity / Magnitude) * Math.Max(Magnitude - (Acceleration * 0.5f * Seconds), 0.0f);
                }
            }

            {
                float Magnitude = LinearVelocity.Length();
                if (Magnitude > TopSpeed)
                {
                    LinearVelocity /= Magnitude;
                    LinearVelocity *= TopSpeed;
                    Magnitude = TopSpeed;
                }

                if (Magnitude > 0.01f)
                {
                    float Remainder = Magnitude * Seconds + 0.1f;
                    Vector3 Dir = Vector3.Normalize(LinearVelocity);

                    for (int i = 0; i < 100 && Remainder > 0.01f; ++i)
                    {
                        (bool Hit, float Travel) = TravelTrace(Eye, Dir, Remainder, 0.125f);
                        Remainder = Math.Max(0.0f, Remainder - Travel);
                        Eye += Dir * Travel;

                        if (Hit)
                        {
                            Vector3 Normal = Gradient(Eye);
                            Normal.Z = 0.0f;
                            float LenSquared = Vector3.Dot(Normal, Normal);
                            if (LenSquared > 0.0f)
                            {
                                Normal /= (float)Math.Sqrt(LenSquared);
                                Dir = Vector3.Reflect(Dir, Normal);
                                LinearVelocity *= 0.75f;
                            }
                            else
                            {
                                Dir = -Dir;
                                LinearVelocity *= 0.5f;
                            }
                        }
                    }

                    Magnitude = LinearVelocity.Length();
                    LinearVelocity = Dir * Magnitude;
                }

                if (Magnitude > 0.01f)
                {
                    GrainAlpha += 1.0f * Seconds;
                }
                else
                {
                    if (GrainAlpha > 0.001f)
                    {
                        GrainAlpha -= 0.25f * Seconds;
                    }
                    else
                    {
                        GrainAlpha = 0.0f;
                    }
                }
                GrainAlpha = Math.Clamp(GrainAlpha, 0.0f, 1.0f);
            }
        }

        WorldToView = Matrix4x4.CreateLookTo(
            Eye,
            EyeDir,
            new Vector3(0, 0, 1));

        {
            const long UpdateTimeSlice = TimeSpan.TicksPerMillisecond * 4;

            long StartTime = DateTime.UtcNow.Ticks;
            long ElapsedTicks = 0;
            int Processed = 0;

            List<(Vector4 Position, Vector4 Normal, Color Color)> SurfelBatch;
            while (ElapsedTicks < UpdateTimeSlice && PendingSurfels.TryDequeue(out SurfelBatch))
            {
                foreach (var Surfel in SurfelBatch)
                {
                    (Vector4 Position, Vector4 Normal, Color Color_) = Surfel;

                    Positions[WriteCursor] = Position;
                    Normals[WriteCursor] = Normal;
                    Colors[WriteCursor] = Color_;
                    WriteCursor = (WriteCursor + 1) % MaxSurfels;
                    LiveSurfels = Math.Min(LiveSurfels + 1, MaxSurfels);
                }
                Processed += SurfelBatch.Count;
                ElapsedTicks = DateTime.UtcNow.Ticks - StartTime;
            }

            double ElapsedCopyTimeMs = (double)ElapsedTicks / (double)TimeSpan.TicksPerMillisecond;

            SplatCopyCount.LogQuantity(Processed);
            SplatCopyTime.LogQuantity(ElapsedCopyTimeMs);
        }

        double CadenceMs = FrameRate.Average();

        double Hz = 1.0 / CadenceMs * 1000.0;

        const long PerfLogFrequency = TimeSpan.TicksPerSecond * 5;
        if (DateTime.UtcNow.Ticks - LastPerfLog >= PerfLogFrequency)
        {
            if (LastPerfLog > 0)
            {
                double UpdatesPerFrame = SplatCopyCount.Average();
                double UpdateProcessingMs = SplatCopyTime.Average();
                double Efficiency = (UpdatesPerFrame / MaxSurfels) * 100.0;

                double ConvergenceTimeMs = ((MaxSurfels / UpdatesPerFrame) - 1) * CadenceMs;

                //TracingRate = Math.Max(MinTracingRate, (int)UpdatesPerFrame);

                Console.Write(
                     "\n\n" +
                     " +- Cadence ------------------------------------------------------------------+\n" +
                     " |\n" +
                    $" |           Frequency : {Math.Round(Hz, 1)} hz\n" +
                    $" |            Interval : {Math.Round(CadenceMs, 1)} ms\n" +
                     " |\n" +
                     " +- Shading ------------------------------------------------------------------+\n" +
                     " |\n" +
                    $" |          Throughput : {Math.Round(UpdatesPerFrame, 0)} ({Math.Round(Efficiency, 2)}%)\n" +
                    $" |           Sync Time : {Math.Round(UpdateProcessingMs, 2)} ms\n" +
                    $" |    Convergence Time : {Math.Round(ConvergenceTimeMs, 2)} ms\n" +
                     " |\n" +
                     " +- Analysis -----------------------------------------------------------------+\n" +
                     " |\n"
                );

                if (Efficiency == 100.0)
                {
                    Console.WriteLine(" |    Convergence time is perfect.");
                }
                else if (UpdateProcessingMs < 4.0)
                {
                    Console.WriteLine(" |    Convergence time is bottlenecked on shading throughput.");
                }
                else if (UpdateProcessingMs >= 4.0)
                {
                    Console.WriteLine(" |    Convergence time is bottlenecked on Synchronization.");
                }
                Console.WriteLine(" |\n +\n");
            }
            LastPerfLog = DateTime.UtcNow.Ticks;
        }

        base.Update(gameTime);
    }

    protected override void Draw(GameTime gameTime)
    {
#if false
        // Clear depth and color.
        GraphicsDevice.Clear(Color.Black);
#else
        // Clear depth only.
        GraphicsDevice.Clear(ClearOptions.DepthBuffer, Color.Black, 1.0f, 0);
#endif

        InstancedBasicEffect.Parameters["LocalToWorld"].SetValue(LocalToWorld);
        //InstancedBasicEffect.Parameters["LocalToWorldRotateOnly"].SetValue(LocalToWorld);
        InstancedBasicEffect.Parameters["WorldToView"].SetValue(WorldToView);
        InstancedBasicEffect.Parameters["ViewToClip"].SetValue(ViewToClip);
        //InstancedBasicEffect.Parameters["EyePosition"].SetValue(Eye);
        InstancedBasicEffect.Parameters["SplatDiameter"].SetValue(
            Lerp(FineDiameter, CoarseDiameter, GrainAlpha * GrainAlpha));
        InstancedBasicEffect.Parameters["SplatDepth"].SetValue(SplatDepth);
        InstancedBasicEffect.Parameters["AspectRatio"].SetValue(1.0f / AspectRatio);

        if (LiveSurfels > 0)
        {
            PositionBuffer.SetData(0, Positions, 0, LiveSurfels, PositionDesc.VertexStride);
            //NormalBuffer.SetData(0, Normals, 0, LiveSurfels, NormalDesc.VertexStride);
            ColorBuffer.SetData(0, Colors, 0, LiveSurfels, ColorDesc.VertexStride);

            GraphicsDevice.RasterizerState = RasterizerState.CullCounterClockwise;
            GraphicsDevice.Indices = VoronoiIndexBuffer;
            GraphicsDevice.SetVertexBuffers(SplatBindings[0], SplatBindings[1], SplatBindings[2], SplatBindings[3]);

            foreach (EffectPass Pass in InstancedBasicEffect.CurrentTechnique.Passes)
            {
                Pass.Apply();
                GraphicsDevice.DrawInstancedPrimitives(PrimitiveType.TriangleList, 0, 0, VoronoiIndexCount, LiveSurfels);
            }
        }

        base.Draw(gameTime);
    }
}

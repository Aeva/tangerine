using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Collections.Concurrent;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

using Color = Microsoft.Xna.Framework.Color;
using Rectangle = Microsoft.Xna.Framework.Rectangle;
using Vector2 = Microsoft.Xna.Framework.Vector2;
using Vector3 = Microsoft.Xna.Framework.Vector3;

using static Microsoft.Xna.Framework.MathHelper;
using System.IO;

namespace ParallelColoring;


public class PerfCounter
{
    private double[] History;
    private int NextSample = 0;
    private int Saturation = 0;
    private long LastUpdate = 0;
    public double Cadence = 0.0;

    public PerfCounter(int HistorySize = 20)
    {
        History = new double[HistorySize];
    }

    public void Reset()
    {
        Saturation = 0;
        NextSample = 0;
        LastUpdate = DateTime.UtcNow.Ticks;
    }

    public void LogFrame()
    {
        long ElapsedTicks = DateTime.UtcNow.Ticks - LastUpdate;
        double ElapsedMs = (double)(ElapsedTicks) / (double)TimeSpan.TicksPerMillisecond;
        LogQuantity(ElapsedMs);
        LastUpdate += ElapsedTicks;
    }

    public void LogQuantity(double Quantity)
    {
        History[NextSample] = Quantity;
        NextSample = (NextSample + 1) % History.Length;
        Saturation = Math.Min(Saturation + 1, History.Length);
    }

    public double Average()
    {
        double Average = 0.0;
        if (Saturation > 0)
        {
            for (int HistoryIndex = 0; HistoryIndex < Saturation; ++HistoryIndex)
            {
                Average += History[HistoryIndex];
            }
            Average /= (double)Saturation;
        }
        return Average;
    }
}


public class Experiment : Game
{
    // The number of subdivisions for the parabola quad like so:
    // 0 -> 0               1
    // 1 -> 0       1       2
    // 2 -> 0   1   2   3   4
    // 3 -> 0 1 2 3 4 5 6 7 8
    private int ParaboloidResolution = 1;

    // Number of voronoi seeds.
    private int SplatCount = 500_000;

    // Target splat size in world space;
    private float SplatSize = 1.0f / 11.0f;

    private bool FullScreen = true;
    private bool VSync = true;

    private GraphicsDeviceManager _graphics;
    private RasterizerState Rasterizer;
    private Effect InstancedBasicEffect;
    private VertexBufferBinding[] SplatBindings;

    private VertexBuffer VoronoiVertexBuffer;
    private IndexBuffer VoronoiIndexBuffer;
    private int VoronoiTriangleCount = 0;
    private int VoronoiVertexCount = 0;
    private int VoronoiIndexCount = 0;

    private VertexBuffer PositionBuffer;
    private VertexBuffer NormalBuffer;
    private VertexBuffer ColorBuffer;

    private Matrix WorldToView;
    private Matrix ViewToClip;

    private float AspectRatio;
    private float SplatScale;

    private PerfCounter FrameRate = new PerfCounter();
    private PerfCounter SplatCopyCount = new PerfCounter();
    private PerfCounter SplatCopyTime = new PerfCounter();
    private long LastPerfLog = 0;

    private CancellationTokenSource CancelSource = new CancellationTokenSource();

    private Vector4[] UploadPositions;
    private Vector4[] UploadNormals;
    private Vector3[] Positions;
    private Vector3[] Normals;
    private Color[] Colors;

    private ConcurrentQueue<(int, Color[])> ColorUpdates;

    private Vector3[] LightPoints = new Vector3[3];
    private Vector3[] LightColors = new Vector3[3];
    private Vector3 Eye = new Vector3(0.0f, 0.0f, 0.0f);

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
            _graphics.PreferredBackBufferWidth = 600;
            _graphics.PreferredBackBufferHeight = 600;
            _graphics.IsFullScreen = false;
            IsMouseVisible = true;
        }

        AspectRatio = (float)_graphics.PreferredBackBufferWidth / (float)_graphics.PreferredBackBufferHeight;
        SplatScale = (float)SplatSize;

        _graphics.ApplyChanges();

        UploadPositions = new Vector4[SplatCount];
        UploadNormals = new Vector4[SplatCount];
        Positions = new Vector3[SplatCount];
        Normals = new Vector3[SplatCount];
        Colors = new Color[SplatCount];

        LightColors[0] = new Vector3(1.0f, 0.0f, 0.0f);
        LightColors[1] = new Vector3(0.0f, 1.0f, 0.0f);
        LightColors[2] = new Vector3(0.0f, 0.0f, 1.0f);

        ColorUpdates = new ConcurrentQueue<(int, Color[])>();
    }

    protected override void Initialize()
    {
        Window.Title = "Star Machine";
        base.Initialize();
    }

    private float EvalSphere(Vector3 Point, float Radius)
    {
        return Point.Length() - Radius;
    }

    private float BlendUnion(float LHS, float RHS, float Width)
    {
        // circular, see https://iquilezles.org/articles/smin/
        Width *= 1.0f / (1.0f - (float)Math.Sqrt(0.5));
        float X = (RHS - LHS)/ Width;
        float G = (X > 1.0f) ? X
            : (X < -1.0f) ? 0.0f
            : 1.0f + 0.5f * (X - (float)Math.Sqrt(2.0f - X * X));
        return RHS - Width * G;
    }

    private float EvalModel(Vector3 Point)
    {
        float Dist = EvalSphere(Point, 2.0f);

        float Offset = 2.1f;
        float StudsDist = EvalSphere(Point - new Vector3(Offset, 0.0f, 0.0f), 0.5f);
        StudsDist = Math.Min(StudsDist, EvalSphere(Point - new Vector3(-Offset, 0.0f, 0.0f), 0.5f));
        StudsDist = Math.Min(StudsDist, EvalSphere(Point - new Vector3(0.0f, 0.0f, Offset), 0.5f));
        StudsDist = Math.Min(StudsDist, EvalSphere(Point - new Vector3(0.0f, 0.0f, -Offset), 0.5f));

        Dist = BlendUnion(Dist, StudsDist, 0.05f);


        float VoidDist = BlendUnion(
            EvalSphere(Point, 1.8f),
            Math.Min(
                EvalSphere(Point - new Vector3(0.0f, -1.75f, 0.0f), 1.5f),
                EvalSphere(Point - new Vector3(0.0f, 1.75f, 0.0f), 1.5f)),
            0.1f);

        Dist = -BlendUnion(-Dist, VoidDist, 0.05f);

        Dist = Math.Min(Dist, EvalSphere(Point - new Vector3(0.0f, 2.0f, 0.0f), 0.5f));
        Dist = Math.Min(Dist, EvalSphere(Point - new Vector3(0.0f, -2.0f, 0.0f), 0.5f));

        return Dist;
    }

    private Vector3 Gradient(Vector3 Point)
    {
        float AlmostZero = 0.0001f;
        var OffsetPNN = new Vector3(AlmostZero, -AlmostZero, -AlmostZero);
        var OffsetNPN = new Vector3(-AlmostZero, AlmostZero, -AlmostZero);
        var OffsetNNP = new Vector3(-AlmostZero, -AlmostZero, AlmostZero);
        var OffsetPPP = new Vector3(AlmostZero, AlmostZero, AlmostZero);

        // Tetrahedral method
        Vector3 Normal =
            OffsetPNN * EvalModel(Point + OffsetPNN) +
            OffsetNPN * EvalModel(Point + OffsetNPN) +
            OffsetNNP * EvalModel(Point + OffsetNNP) +
            OffsetPPP * EvalModel(Point + OffsetPPP);

        float LengthSquared = Vector3.Dot(Normal, Normal);
        if (LengthSquared == 0.0)
        {
            // Gradient is zero.  Let's try again with a worse method.
            float Dist = EvalModel(Point);
            return Vector3.Normalize(new Vector3(
                EvalModel(Point + OffsetPNN) - Dist,
                EvalModel(Point + OffsetNPN) - Dist,
                EvalModel(Point + OffsetNNP) - Dist));
        }
        else
        {
            return Normal / (float)Math.Sqrt(LengthSquared);
        }
    }

    private (bool, Vector3) Trace (Vector3 Start, Vector3 Stop)
    {
        Vector3 Point = Start;
        Vector3 Dir = Vector3.Normalize(Stop - Start);
        float Travel = 0.0f;
        for (int Iteration = 0; Iteration < 100; ++Iteration)
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

    private void PopulateSplats()
    {
        var SplatRNG = new Random();

        var parallelOptions = new ParallelOptions();
        parallelOptions.CancellationToken = CancelSource.Token;
        parallelOptions.MaxDegreeOfParallelism = Math.Max(Environment.ProcessorCount - 1, 1);

        Parallel.For(0, UploadPositions.Length, parallelOptions, (Cursor) =>
        {
            while (true)
            {
#if false
                var Start = Vector3.Normalize(new Vector3(
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f,
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f,
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f)) * 3.0f;
                if (ModelEval(Start) < 0.0f)
                {
                    continue;
                }
#else
                var Start = new Vector3(
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f,
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f,
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f) * 10.0f;
#endif
                var Stop = Vector3.Normalize(new Vector3(
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f,
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f,
                    ((float)SplatRNG.Next(-1000, 1000)) / 1000.0f)) * 4.0f;

                if (Start != Stop)
                {
                    (bool Hit, Vector3 Position) = Trace(Start, Stop);
                    if (Hit)
                    {
                        var Normal = Gradient(Position);
                        UploadPositions[Cursor] = new Vector4(Position, 1.0f);
                        UploadNormals[Cursor] = new Vector4(Normal, 1.0f);
                        Positions[Cursor] = Position;
                        Normals[Cursor] = Normal;
                        Colors[Cursor] = new Color(Normal.X * 0.5f + 0.5f, Normal.Y * 0.5f + 0.5f, Normal.Z * 0.5f + 0.5f);
                        break;
                    }
                }
            }
        });
    }

    private void ColorizeSplat(int SplatIndex, int OutputIndex, Color[] NewColors)
    {
        var Position = Positions[SplatIndex];
        var Normal = Normals[SplatIndex];

        var EyeRay = Vector3.Normalize(Eye - Position);
        var Pivot = Vector3.Dot(EyeRay, Normal);

        if (Pivot >= -0.1f)
        {
            var SplatColor = new Vector3(0.0f, 0.0f, 0.0f);
            for (int LightIndex = 0; LightIndex < LightPoints.Length; ++LightIndex)
            {
                var LightPoint = LightPoints[LightIndex];
                var LightColor = LightColors[LightIndex];


                var Offset = Normal * 0.01f + Position;
                (bool Hit, Vector3 OcclusionPoint) = Trace(Offset, LightPoint);
                if (Hit)
                {
                    NewColors[OutputIndex] = Color.Black;
                }
                else
                {
                    var LightRay = Vector3.Normalize(LightPoint - Position);

                    float Luminence = Math.Max(Vector3.Dot(LightRay, Normal), 0.0f);

                    SplatColor += LightColor * Luminence;
                }
            }
            NewColors[OutputIndex] = new Color(SplatColor);
        }
#if false
        else
        {
            NewColors[OutputIndex] = Color.Magenta;
        }
#endif
    }

    protected override void LoadContent()
    {
        {
            byte[] ShaderBytes = File.ReadAllBytes("Shaders/InstancedSplats.ogl.mgfxo");
            InstancedBasicEffect = new Effect(GraphicsDevice, ShaderBytes);
            InstancedBasicEffect.CurrentTechnique = InstancedBasicEffect.Techniques[0];
        }

        {
            WorldToView = Matrix.CreateLookAt(new Vector3(0, 0, 10), new Vector3(0, 0, 0), new Vector3(0, 1, 0));
            ViewToClip = Matrix.CreatePerspectiveFieldOfView(MathHelper.ToRadians(45), AspectRatio, 0.01f, 1000.0f);
        }

        {
            Rasterizer = new RasterizerState();
            Rasterizer.CullMode = CullMode.CullCounterClockwiseFace;
        }

        {
            int EdgesPerAxis = 1 << ParaboloidResolution;
            int VerticesPerAxis = EdgesPerAxis + 1;
            VoronoiVertexCount = VerticesPerAxis * VerticesPerAxis;
            VoronoiTriangleCount = EdgesPerAxis * EdgesPerAxis * 2;
            VoronoiIndexCount = VoronoiTriangleCount * 3;
            {
                var ParaboloidIndices = new short[VoronoiIndexCount];
                int Cursor = 0;
                for (int QuadY = 0; QuadY < EdgesPerAxis; ++QuadY)
                {
                    for (int QuadX = 0; QuadX < EdgesPerAxis; ++QuadX)
                    {
                        short Vert00 = (short)((QuadY + 0) * VerticesPerAxis + (QuadX + 0));
                        short Vert01 = (short)((QuadY + 0) * VerticesPerAxis + (QuadX + 1));
                        short Vert10 = (short)((QuadY + 1) * VerticesPerAxis + (QuadX + 0));
                        short Vert11 = (short)((QuadY + 1) * VerticesPerAxis + (QuadX + 1));

                        ParaboloidIndices[Cursor++] = Vert00;
                        ParaboloidIndices[Cursor++] = Vert10;
                        ParaboloidIndices[Cursor++] = Vert01;

                        ParaboloidIndices[Cursor++] = Vert11;
                        ParaboloidIndices[Cursor++] = Vert01;
                        ParaboloidIndices[Cursor++] = Vert10;
                    }
                }

                VoronoiIndexBuffer = new IndexBuffer(
                    GraphicsDevice,
                    IndexElementSize.SixteenBits,
                    sizeof(short) * VoronoiIndexCount,
                    BufferUsage.WriteOnly);
                VoronoiIndexBuffer.SetData<short>(ParaboloidIndices);
            }
            {
                var ParaboloidVertices = new VertexPosition[VoronoiVertexCount];
                float UnitScale = 1.0f / (float)(EdgesPerAxis);
                var PositionScale = new Vector3(SplatScale, SplatScale, 0.1f);
                var Center = new Vector2(0.5f, 0.5f);
                int Index = 0;

                for (int Y = 0; Y < VerticesPerAxis; ++Y)
                {
                    for (int X = 0; X < VerticesPerAxis; ++X)
                    {
                        var Position = new Vector3();
                        float U = (float)X * UnitScale;
                        float V = (float)Y * UnitScale;
                        U = (U * 2.0f - 1.0f);
                        V = (V * 2.0f - 1.0f);

                        // TODO generate the paraboloids as discs not grids
                        float Len = (float)Math.Sqrt(U * U + V * V);
                        if (Len > 1.0f)
                        {
                            U /= Len;
                            V /= Len;
                        }

                        Position.X = Center.X * U;
                        Position.Y = Center.Y * V;
                        Position.Z = 1 - (U * U + V * V);
                        ParaboloidVertices[Index++].Position = Position * PositionScale;
                    }
                }

                VoronoiVertexBuffer = new VertexBuffer(
                    GraphicsDevice,
                    typeof(VertexPosition),
                    VoronoiVertexCount,
                    BufferUsage.WriteOnly);
                VoronoiVertexBuffer.SetData<VertexPosition>(ParaboloidVertices);
            }
        }

        {
            var VertexPosition = new VertexDeclaration(
                new VertexElement(0, VertexElementFormat.Vector4, VertexElementUsage.TextureCoordinate, 0));

            var VertexNormal = new VertexDeclaration(
                new VertexElement(0, VertexElementFormat.Vector4, VertexElementUsage.Normal, 0));

            var VertexColor = new VertexDeclaration(
                new VertexElement(0, VertexElementFormat.Color, VertexElementUsage.Color, 0));

            PositionBuffer = new VertexBuffer(GraphicsDevice, VertexPosition, SplatCount, BufferUsage.WriteOnly);
            NormalBuffer = new VertexBuffer(GraphicsDevice, VertexNormal, SplatCount, BufferUsage.WriteOnly);
            ColorBuffer = new VertexBuffer(GraphicsDevice, VertexColor, SplatCount, BufferUsage.WriteOnly);

            PopulateSplats();

            PositionBuffer.SetData(0, UploadPositions, 0, SplatCount, VertexPosition.VertexStride);
            NormalBuffer.SetData(0, UploadNormals, 0, SplatCount, VertexPosition.VertexStride);
            ColorBuffer.SetData(0, Colors, 0, SplatCount, VertexColor.VertexStride);
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

            var CeilDivide = (int Numerator, int Denominator) =>
            {
                return (Numerator + Denominator - 1) / Denominator;
            };

            int SliceSize = SplatCount;
            int SliceCount = 1;

            if (SplatCount > parallelOptions.MaxDegreeOfParallelism)
            {
                int MaximumBatchSize = 50;
                SliceSize = Math.Min(CeilDivide(SplatCount, parallelOptions.MaxDegreeOfParallelism), MaximumBatchSize);
                SliceCount = CeilDivide(SplatCount, SliceSize);
                SliceSize = CeilDivide(SplatCount, SliceCount);
            }

            Debug.Assert(SplatCount <= SliceCount * SliceSize);

            Task.Run(() => {
                while(!parallelOptions.CancellationToken.IsCancellationRequested)
                {
                    if (ColorUpdates.Count == 0)
                    {
                        Parallel.For(0, SliceCount, parallelOptions, (SliceIndex) =>
                        {
                            int Start = SliceIndex * SliceSize;
                            int Stop = Math.Min(SplatCount, Start + SliceSize);
                            int Range = Stop - Start;
                            var NewColors = new Color[Range];
                            for (int BatchIndex = 0; BatchIndex < Range; ++BatchIndex)
                            {
                                int SplatIndex = Start + BatchIndex;
                                ColorizeSplat(SplatIndex, BatchIndex, NewColors);
                            }
                            ColorUpdates.Enqueue((Start, NewColors));
                        });
                    }
                    else
                    {
                        Thread.Sleep(1);
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
        if (GamePad.GetState(PlayerIndex.One).Buttons.Back == ButtonState.Pressed || Keyboard.GetState().IsKeyDown(Keys.Escape))
        {
            CancelSource.Cancel();
            Exit();
        }

        FrameRate.LogFrame();

        var FindLightPosition = (double Speed, double Phase) =>
        {
            double T = gameTime.TotalGameTime.TotalMilliseconds / 5000.0;
            double P = 2.0 * Math.PI * Phase;
            float S = (float)Math.Sin(T * Speed + P);
            float C = (float)Math.Cos(T * Speed + P);
            return new Vector3(S * 15.0f, C * 15.0f, 10.0f);
        };

        LightPoints[0] = FindLightPosition(1.0, 0.0 / 3.0);
        LightPoints[1] = FindLightPosition(2.0, 1.0 / 3.0);
        LightPoints[2] = FindLightPosition(-4.0, 2.0 / 3.0);

        {
            float T = (float)gameTime.TotalGameTime.TotalMilliseconds / -10000.0f * (float)Math.PI;
            float S = (float)Math.Sin(T);
            float C = (float)Math.Cos(T);
            Eye = new Vector3(S * 8.0f, C * 8.0f, 1.0f);
        }

        {
            const long UpdateTimeSlice = TimeSpan.TicksPerMillisecond * 4;

            long StartTime = DateTime.UtcNow.Ticks;
            long ElapsedTicks = 0;
            int Processed = 0;

            (int, Color[]) ColorUpdate;
            while (ElapsedTicks < UpdateTimeSlice && ColorUpdates.TryDequeue(out ColorUpdate))
            {
                (int StartOffset, Color[] Updates) = ColorUpdate;
                for (int UpdateIndex = 0; UpdateIndex < Updates.Length; ++UpdateIndex)
                {
                    Colors[StartOffset + UpdateIndex] = Updates[UpdateIndex];
                }
                Processed += Updates.Length;
                ElapsedTicks = DateTime.UtcNow.Ticks - StartTime;
            }

            double ElapsedTimeMs = (double)ElapsedTicks / (double)TimeSpan.TicksPerMillisecond;

            SplatCopyCount.LogQuantity(Processed);
            SplatCopyTime.LogQuantity(ElapsedTimeMs);
        }

        double CadenceMs = FrameRate.Average();

        double Hz = 1.0 / CadenceMs * 1000.0;
        //Window.Title = $"Star Machine {Math.Round(Hz, 0)} fps";

        const long PerfLogFrequency = TimeSpan.TicksPerSecond * 5;
        if (DateTime.UtcNow.Ticks - LastPerfLog >= PerfLogFrequency)
        {
            if (LastPerfLog > 0)
            {
                double UpdatesPerFrame = SplatCopyCount.Average();
                double UpdateProcessingMs = SplatCopyTime.Average();
                double Efficiency = (UpdatesPerFrame / SplatCount) * 100.0;

                double ConvergenceTimeMs = ((SplatCount / UpdatesPerFrame) - 1) * CadenceMs;

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
        GraphicsDevice.Clear(new Color(0.0f, 0.0f, 0.0f));

        WorldToView = Matrix.CreateLookAt(
            Eye,
            new Vector3(0, 0, 0),
            new Vector3(0, 0, 1));

        InstancedBasicEffect.Parameters["WorldToView"].SetValue(WorldToView);
        InstancedBasicEffect.Parameters["ViewToClip"].SetValue(ViewToClip);
        InstancedBasicEffect.Parameters["EyePosition"].SetValue(Eye);

        ColorBuffer.SetData(0, Colors, 0, SplatCount, 4 /*VertexColor.VertexStride*/);

        GraphicsDevice.RasterizerState = Rasterizer;
        GraphicsDevice.Indices = VoronoiIndexBuffer;
        GraphicsDevice.SetVertexBuffers(SplatBindings[0], SplatBindings[1], SplatBindings[2], SplatBindings[3]);

        foreach (EffectPass Pass in InstancedBasicEffect.CurrentTechnique.Passes)
        {
            Pass.Apply();
            GraphicsDevice.DrawInstancedPrimitives(PrimitiveType.TriangleList, 0, 0, VoronoiIndexCount, SplatCount);
        }

        base.Draw(gameTime);
    }
}

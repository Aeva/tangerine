using System;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

using Color = Microsoft.Xna.Framework.Color;
using Rectangle = Microsoft.Xna.Framework.Rectangle;
using Vector2 = Microsoft.Xna.Framework.Vector2;
using Vector3 = Microsoft.Xna.Framework.Vector3;

using static Microsoft.Xna.Framework.MathHelper;
using System.IO;

namespace HelloVoronoi;


public class Experiment : Game
{
    // The number of subdivisions for the parabola quad like so:
    // 0 -> 0               1
    // 1 -> 0       1       2
    // 2 -> 0   1   2   3   4
    // 3 -> 0 1 2 3 4 5 6 7 8
    private int ParaboloidResolution = 1;

    // Number of voronoi seeds.
    private int SplatCount = 1_000_000;

    // Target splat size in pixels;
    private int SplatSize = 32;

    private bool FullScreen = true;

    private GraphicsDeviceManager _graphics;
    private RasterizerState Rasterizer;
    private Effect InstancedBasicEffect;
    private VertexBufferBinding[] SplatBindings;

    private VertexBuffer VoronoiVertexBuffer;
    private IndexBuffer VoronoiIndexBuffer;
    private int VoronoiTriangleCount = 0;
    private int VoronoiVertexCount = 0;
    private int VoronoiIndexCount = 0;

    private VertexBuffer SplatBuffer;

    private Matrix WorldToView;
    private Matrix ViewToClip;

    private float AspectRatio;
    private float SplatScale;

    private double[] FrameHistory = new double[20];
    private int FrameNumber = 0;
    private double Cadence;

    public Experiment()
    {
        _graphics = new GraphicsDeviceManager(this);
        Content.RootDirectory = "Content";
        IsMouseVisible = true;
        IsFixedTimeStep = false;

        _graphics.HardwareModeSwitch = false;
        _graphics.GraphicsProfile = GraphicsProfile.HiDef;
        _graphics.SynchronizeWithVerticalRetrace = false;

        SplatBindings = new VertexBufferBinding[2];

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

        AspectRatio = (float)_graphics.PreferredBackBufferHeight / (float)_graphics.PreferredBackBufferWidth;
        float LargestDimension = Math.Max((float)_graphics.PreferredBackBufferHeight, (float)_graphics.PreferredBackBufferWidth);
        SplatScale = ((float)SplatSize / LargestDimension);

        _graphics.ApplyChanges();
    }

    protected override void Initialize()
    {
        Window.Title = "Star Machine";
        base.Initialize();
    }

    protected override void LoadContent()
    {
        {
            byte[] ShaderBytes = File.ReadAllBytes("Shaders/InstancedSplats.ogl.mgfxo");
            InstancedBasicEffect = new Effect(GraphicsDevice, ShaderBytes);
            InstancedBasicEffect.CurrentTechnique = InstancedBasicEffect.Techniques[0];
        }

        {
            WorldToView = Matrix.CreateLookAt(new Vector3(0, 0, 3), new Vector3(0, 0, 0), new Vector3(0, 1, 0));
            ViewToClip = Matrix.CreateOrthographic(2.0f, 2.0f * AspectRatio, 0.001f, 20.0f);
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
                var PositionScale = new Vector3(SplatScale, SplatScale, 1.0f);
                var Center = new Vector2(0.5f, 0.5f);
                int Index = 0;

                for (int Y = 0; Y < VerticesPerAxis; ++Y)
                {
                    for (int X = 0; X < VerticesPerAxis; ++X)
                    {
                        var Position = new Vector3();
                        float U = (float)X * UnitScale;
                        float V = (float)Y * UnitScale;
                        Position.X = U - Center.X;
                        Position.Y = V - Center.Y;
                        U = (U * 2.0f - 1.0f);
                        V = (V * 2.0f - 1.0f);
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
            var SplatRNG = new Random(1234);
            var StarRNG = new Random(75828421);
            var ColorRNG = new Random(0);

            var Offsets = new Vector4[SplatCount];
            var Colors = new Color[SplatCount];

            var StarMachine = (Vector2 Point, float r, float rf) =>
            {
                var k1 = new Vector2(0.809016994375f, -0.587785252292f);
                var k2 = new Vector2(-k1.X,k1.Y);
                Point.X = Math.Abs(Point.X);
                Point -= k1 * 2.0f * Max(Vector2.Dot(k1, Point), 0.0f);
                Point -= k2 * 2.0f * Max(Vector2.Dot(k2, Point), 0.0f);
                Point.X = Math.Abs(Point.X);
                Point.Y -= r;
                var ba = new Vector2(rf * -k1.Y, rf * k1.X - 1.0f);
                float h = Clamp(Vector2.Dot(Point, ba) / Vector2.Dot(ba,ba), 0.0f, r );
                float sign = Point.Y * ba.X - Point.X * ba.Y >= 0.0f ? 1.0f : -1.0f;
                return (Point - ba * h).Length() * sign;
            };

            int FieldW = 7;
            int FieldH = (int)((float)FieldW * AspectRatio);
            float FieldSlice = 2.0f / (float)FieldW;
            float HalfSlice = FieldSlice * 0.5f;
            Vector2 FieldOrigin = new Vector2(-1.0f, -AspectRatio + HalfSlice);

            var StarPoints = new Vector2[175];
            var StarColors = new Color[StarPoints.Length];
            for (int StarIndex = 0; StarIndex < StarPoints.Length; ++StarIndex)
            {
                while (true)
                {
                    float Alpha = StarRNG.NextSingle();
                    Alpha = Lerp((float)Math.Sqrt(Alpha), Alpha, 0.3f);
                    StarPoints[StarIndex].X = ((float)StarRNG.Next(-1000, 1000)) / 900.0f;
                    StarPoints[StarIndex].Y = Lerp(-AspectRatio, AspectRatio, Alpha);

                    var Point = StarPoints[StarIndex];
                    {
                        float Angle = ToRadians(10.0f);
                        float C = (float)Math.Cos(Angle);
                        float S = (float)Math.Sin(Angle);
                        Point = new Vector2(Point.X * C - Point.Y * S, Point.X * S + Point.Y * C);
                    }

                    float Dist = StarMachine(Point, 0.6f, 0.45f);

                    if (Dist >= 0.025f)
                    {
                        if (ColorRNG.Next(100) <= 30)
                        {
                            StarColors[StarIndex] = Color.White;
                        }
                        else
                        {
                            float R = ColorRNG.NextSingle() * 0.7f + 0.3f;
                            float G = ColorRNG.NextSingle() * 0.7f + 0.3f;
                            float B = ColorRNG.NextSingle() * 0.7f + 0.3f;
                            float A = Max(Max(R, G), B);

                            StarColors[StarIndex] = new Color(R / A, B / A, G / A);
                        }
                        break;
                    }
                }
            }

            var StarSpray = (Vector2 Splat) =>
            {
                float Dist = 1000000.0f;
                Color StarColor = Color.White;
                for (int StarIndex = 0; StarIndex < StarPoints.Length; ++StarIndex)
                {
                    var Star = StarPoints[StarIndex];
                    float NewDist = Vector2.Distance(Splat, Star);
                    if (NewDist < Dist)
                    {
                        Dist = NewDist;
                        StarColor = StarColors[StarIndex];
                    }
                }
                return (Dist, StarColor);
            };

            for (int CellIndex = 0; CellIndex < SplatCount; ++CellIndex)
            {
                var Offset = new Vector3(
                    ((float)SplatRNG.Next(-1000, 1000)) / 900.0f,
                    ((float)SplatRNG.Next(-1000, 1000)) / 900.0f * AspectRatio,
                    0.0f);

                var Point = new Vector2(Offset.X, Offset.Y);
                {
                    float Angle = ToRadians(10.0f);
                    float C = (float)Math.Cos(Angle);
                    float S = (float)Math.Sin(Angle);
                    Point = new Vector2(Point.X * C - Point.Y * S, Point.X * S + Point.Y * C);
                }

                float Dist = StarMachine(Point, 0.6f, 0.45f);
                Color Fill;
                if (Dist <= 0.0f)
                {
                    // Draw the star cutout
                    float Angle = ToRadians(13.0f);
                    float C = (float)Math.Cos(Angle);
                    float S = (float)Math.Sin(Angle);

                    Point = new Vector2(Offset.X - 0.03f, Offset.Y + 0.275f);
                    Point = new Vector2(Point.X * C - Point.Y * S, Point.X * S + Point.Y * C);

                    Dist = StarMachine(Point, 0.6f, 0.45f);

                    if (Dist <= 0.0f)
                    {
                        // Falling cutaway
                        Dist = Vector3.Distance(Offset, new Vector3(-0.00f, -0.01f, 0.0f)) * 1.75f;
                        float Alpha = Clamp(Dist, 0.0f, 1.0f);
                        for (int Repeat = 0; Repeat < 2; ++Repeat)
                        {
                            Alpha = Alpha * Alpha;
                        }

                        var FG = Color.White;
                        var BG = Color.CornflowerBlue;
                        Fill = Color.Lerp(FG, BG, Alpha);
                    }
                    else
                    {
                        // Daytime atmosphere
                        Dist = Vector3.Distance(Offset, new Vector3(0.0f, -3.8f, 0.0f)) * 0.25f;
                        float Alpha = Clamp(Dist, 0.0f, 1.0f);
                        for (int Repeat = 0; Repeat < 3; ++Repeat)
                        {
                            Alpha = Alpha * Alpha;
                        }

                        var FG = Color.White;
                        var BG = Color.CornflowerBlue;
                        Fill = Color.Lerp(FG, BG, Alpha);
                    }
                }
                else
                {
                    // Atmosphere gradient
                    Dist = Vector3.Distance(Offset, new Vector3(0.0f, -10.0f, 0.0f)) * 0.1f;
                    float Alpha = Clamp(Dist, 0.0f, 1.0f);
                    for (int Repeat = 0; Repeat < 2; ++Repeat)
                    {
                        Alpha = Alpha * Alpha;
                    }

                    Color Atmosphere;
                    {
                        var FG = new Color(0.5f, 0.7f, 1.0f);
                        var BG = new Color(0.0f, 0.05f, 0.1f);
                        Atmosphere = Color.Lerp(FG, BG, Alpha);
                    }

                    // Sky and small stars
                    (Dist, Color StarColor) = StarSpray(new Vector2(Offset.X, Offset.Y));
                    float OutterSize = Lerp(0.0f, 0.004f, Alpha * Alpha);
                    Dist -= OutterSize;

                    float Threshold = Lerp(0.1f, 0.0f, Alpha * Alpha);
                    if (Dist > Threshold)
                    {
                        Fill = Atmosphere;
                    }
                    else if (Threshold > 0.0f)
                    {
                        var Blur = 1.0f - Clamp(Dist, 0.0f, Threshold) / Threshold;
                        Fill = Color.Lerp(Atmosphere, StarColor, Blur * Blur * Blur * Blur * Alpha);
                    }
                    else
                    {
                        Fill = StarColor;
                    }
                }

                Offsets[CellIndex] = new Vector4(Offset, 1.0f);
                Colors[CellIndex] = Fill;
            }

            var VertexOffsetColor = new VertexDeclaration(
                new VertexElement(0, VertexElementFormat.Vector4, VertexElementUsage.TextureCoordinate, 0),
                new VertexElement(16, VertexElementFormat.Color, VertexElementUsage.Color, 0));

            SplatBuffer = new VertexBuffer(GraphicsDevice, VertexOffsetColor, SplatCount, BufferUsage.WriteOnly);
            SplatBuffer.SetData(0, Offsets, 0, SplatCount, VertexOffsetColor.VertexStride);
            SplatBuffer.SetData(16, Colors, 0, SplatCount, VertexOffsetColor.VertexStride);
        }

        {
            SplatBindings[0] = new VertexBufferBinding(VoronoiVertexBuffer, 0, 0);
            SplatBindings[1] = new VertexBufferBinding(SplatBuffer, 0, 1);
        }
    }

    protected override void Update(GameTime gameTime)
    {
        if (GamePad.GetState(PlayerIndex.One).Buttons.Back == ButtonState.Pressed || Keyboard.GetState().IsKeyDown(Keys.Escape))
        {
            Exit();
        }

        int Cursor = (FrameNumber++) % FrameHistory.Length;
        int HistorySize = Min(FrameNumber, FrameHistory.Length);
        FrameHistory[Cursor] = gameTime.ElapsedGameTime.TotalMilliseconds;

        Cadence = 0.0;
        for (int Frame = 0; Frame < HistorySize; ++Frame)
        {
            Cadence += FrameHistory[Frame];
        }
        Cadence /= (double)HistorySize;
        double Hz = 1.0 / Cadence * 1000.0;

        Window.Title = $"Star Machine {Math.Round(Hz, 1)} fps";

        base.Update(gameTime);
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(new Color(0.0f, 0.0f, 0.0f));

        InstancedBasicEffect.Parameters["WorldToView"].SetValue(WorldToView);
        InstancedBasicEffect.Parameters["ViewToClip"].SetValue(ViewToClip);

        GraphicsDevice.RasterizerState = Rasterizer;
        GraphicsDevice.Indices = VoronoiIndexBuffer;
        GraphicsDevice.SetVertexBuffers(SplatBindings[0], SplatBindings[1]);

        foreach (EffectPass Pass in InstancedBasicEffect.CurrentTechnique.Passes)
        {
            Pass.Apply();
            GraphicsDevice.DrawInstancedPrimitives(PrimitiveType.TriangleList, 0, 0, VoronoiIndexCount, SplatCount);
        }

        base.Draw(gameTime);
    }
}

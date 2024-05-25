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

namespace SplatCasting;


public class Experiment : Game
{
    // The number of subdivisions for the parabola quad like so:
    // 0 -> 0               1
    // 1 -> 0       1       2
    // 2 -> 0   1   2   3   4
    // 3 -> 0 1 2 3 4 5 6 7 8
    private int ParaboloidResolution = 3;

    // Number of voronoi seeds.
    private int SplatCount = 20_500;

    // Target splat size in world space;
    private float SplatSize = 0.125f;

    private bool FullScreen = false;

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
        _graphics.SynchronizeWithVerticalRetrace = true;

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

        AspectRatio = (float)_graphics.PreferredBackBufferWidth / (float)_graphics.PreferredBackBufferHeight;
        SplatScale = (float)SplatSize;

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
            var SplatRNG = new Random(0);
            var Offsets = new Vector4[SplatCount];
            var Colors = new Color[SplatCount];

            var SphereEval = (Vector3 Point, float Radius) =>
            {
                return Point.Length() - Radius;
            };

            var ModelEval = (Vector3 Point) =>
            {
                /*
                inter(cube(4), sphere(5.5))
                :diff(
                    cylinder(3, 5),
                    cylinder(3, 5):rotate_x(90),
                    cylinder(3, 5):rotate_y(90))
                 */
                //return Math.Max(BoxEval(Point, new Vector3(4.0f, 4.0f, 4.0f)),
                //                -CylinderEval(Point, 3.0f, 4.0f));
                float Dist = SphereEval(Point, 2.0f);

                Dist = Math.Min(Dist, SphereEval(Point - new Vector3(2.0f, 0.0f, 0.0f), 0.5f));
                Dist = Math.Min(Dist, SphereEval(Point - new Vector3(-2.0f, 0.0f, 0.0f), 0.5f));
                Dist = Math.Min(Dist, SphereEval(Point - new Vector3(0.0f, 0.0f, 2.0f), 0.5f));
                Dist = Math.Min(Dist, SphereEval(Point - new Vector3(0.0f, 0.0f, -2.0f), 0.5f));

                Dist = Math.Max(Dist, -SphereEval(Point, 1.9f));
                Dist = Math.Max(Dist, -SphereEval(Point - new Vector3(0.0f, 1.0f, 0.0f), 1.5f));
                Dist = Math.Max(Dist, -SphereEval(Point - new Vector3(0.0f, -1.0f, 0.0f), 1.5f));

                Dist = Math.Min(Dist, SphereEval(Point - new Vector3(0.0f, 2.0f, 0.0f), 0.5f));
                Dist = Math.Min(Dist, SphereEval(Point - new Vector3(0.0f, -2.0f, 0.0f), 0.5f));

                return Dist;
            };

            var Trace = (Vector3 Start, Vector3 Stop) =>
            {
                Vector3 Point = Start;
                Vector3 Dir = Vector3.Normalize(Stop - Start);
                float Travel = 0.0f;
                for (int Iteration = 0; Iteration < 100; ++Iteration)
                {
                    float Dist = ModelEval(Point);
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
            };

            int Accepted = 0;
            while (Accepted < Offsets.Length)
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
                        Offsets[Accepted] = new Vector4(Position, 1.0f);
                        Colors[Accepted] = new Color(SplatRNG.NextSingle(), SplatRNG.NextSingle(), SplatRNG.NextSingle());
                        ++Accepted;
                    }
                }
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

        float T = (float)gameTime.TotalGameTime.TotalMilliseconds / -10000.0f * (float)Math.PI;
        float S = (float)Math.Sin(T);
        float C = (float)Math.Cos(T);

        WorldToView = Matrix.CreateLookAt(
            new Vector3(S * 8.0f, C * 8.0f, 1.0f),
            new Vector3(0, 0, 0),
            new Vector3(0, 0, 1));

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

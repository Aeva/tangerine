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
    private int Cells = 100_000;

    private GraphicsDeviceManager _graphics;
    private Effect InstancedBasicEffect;

    private VertexBuffer VoronoiVertexBuffer;
    private IndexBuffer VoronoiIndexBuffer;
    private int VoronoiTriangleCount = 0;
    private int VoronoiVertexCount = 0;

    private VertexBuffer SplatBuffer;
    private int SplatCount = 0;

    private Matrix WorldToView;
    private Matrix ViewToClip;

    private float AspectRatio;

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
        //_graphics.SynchronizeWithVerticalRetrace = false;
#if true
        _graphics.PreferredBackBufferWidth = GraphicsAdapter.DefaultAdapter.CurrentDisplayMode.Width;
        _graphics.PreferredBackBufferHeight = GraphicsAdapter.DefaultAdapter.CurrentDisplayMode.Height;
        _graphics.IsFullScreen = true;
        IsMouseVisible = false;
#else
        _graphics.PreferredBackBufferWidth = 600;
        _graphics.PreferredBackBufferHeight = 600;
        _graphics.IsFullScreen = false;
        IsMouseVisible = true;
#endif
        AspectRatio = (float)_graphics.PreferredBackBufferHeight / (float)_graphics.PreferredBackBufferWidth;

        _graphics.ApplyChanges();
    }

    protected override void Initialize()
    {
        Window.Title = "Voronoi Diagram Rendering";
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
            int Edges = 1 << ParaboloidResolution;
            VoronoiTriangleCount = Edges * Edges * 2;
            VoronoiVertexCount = VoronoiTriangleCount * 3;

            {
                var ParaboloidIndices = new short[VoronoiVertexCount];
                for (short Index = 0; Index < VoronoiVertexCount; Index++)
                {
                    ParaboloidIndices[Index] = Index;
                }
                VoronoiIndexBuffer = new IndexBuffer(
                    GraphicsDevice,
                    IndexElementSize.SixteenBits,
                    sizeof(short) * ParaboloidIndices.Length,
                    BufferUsage.WriteOnly);
                VoronoiIndexBuffer.SetData<short>(ParaboloidIndices);
            }
            {
                var ParaboloidVertices = new VertexPosition[VoronoiVertexCount];
                {
                    Vector2[] QuadVerts =
                    {
                        new Vector2(0.0f, 0.0f),
                        new Vector2(0.0f, 1.0f),
                        new Vector2(1.0f, 0.0f),

                        new Vector2(1.0f, 0.0f),
                        new Vector2(0.0f, 1.0f),
                        new Vector2(1.0f, 1.0f)
                    };

                    float Scale = 1.0f / (float)(Edges);
                    var Center = new Vector2(0.5f, 0.5f);
                    int Index = 0;

                    for (int Y = 0; Y < Edges; ++Y)
                    {
                        for (int X = 0; X < Edges; ++X)
                        {
                            var Offset = new Vector2((float)X * Scale, (float)Y * Scale);
                            foreach (var Corner in QuadVerts)
                            {
                                var Position = new Vector3();
                                float U = QuadVerts[Index % 6].X * Scale + Offset.X;
                                float V = QuadVerts[Index % 6].Y * Scale + Offset.Y;
                                Position.X = U - Center.X;
                                Position.Y = V - Center.Y;
                                U = (U * 2.0f - 1.0f);
                                V = (V * 2.0f - 1.0f);
                                Position.Z = 1 - (U * U + V * V);
                                ParaboloidVertices[Index++].Position = Position;
                            }
                        }
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
            var RNG = new Random(1234);

            var Offsets = new Vector4[Cells];
            var Colors = new Color[Cells];

            for (int CellIndex = 0; CellIndex < Cells; ++CellIndex)
            {
                var Offset = new Vector3(
                    ((float)RNG.Next(-1000, 1000)) / 900.0f,
                    ((float)RNG.Next(-1000, 1000)) / 900.0f * AspectRatio,
                    0.0f);

                float Radius = 0.5f;
                float Fnord = Math.Abs(Offset.Length() - Radius);
                float Alpha = Clamp((Offset.Length() - Radius) * 50.0f, 0.0f, 1.0f);
                float Relief = (float)Math.Sqrt(
                    1.0f - ((Offset.X * 2.0f) * (Offset.X * 2.0f) + (Offset.Y * 2.0f) * (Offset.Y * 2.0f)));

                Offset.Z = Max(Fnord * Fnord, 0.0f);
                var Norm = Vector3.Normalize(Offset);
                var Specular = Vector3.Dot(new Vector3(0.5f, 0.0f, 10.0f), Norm);

                var BG = Color.Lerp(Color.Green, Color.Blue, Offset.Y * 0.5f + 0.4f);
                //var FG = new Color(Specular, Specular, Specular);
                //var FG = new Color(Norm.X, Norm.Y, Norm.Z);
                var FG = new Color(Fnord, Fnord, Fnord) * 1.5f;

                var Fill = Color.Lerp(FG, BG, Alpha);

                Offsets[CellIndex] = new Vector4(Offset, 1.0f);
                Colors[CellIndex] = Fill;
            }

            var VertexOffsetColor = new VertexDeclaration(
                new VertexElement(0, VertexElementFormat.Vector4, VertexElementUsage.TextureCoordinate, 0),
                new VertexElement(16, VertexElementFormat.Color, VertexElementUsage.Color, 0));

            SplatBuffer = new VertexBuffer(GraphicsDevice, VertexOffsetColor, Cells, BufferUsage.WriteOnly);
            SplatBuffer.SetData(0, Offsets, 0, Cells, VertexOffsetColor.VertexStride);
            SplatBuffer.SetData(16, Colors, 0, Cells, VertexOffsetColor.VertexStride);
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

        Window.Title = $"Voronoi Diagram Rendering {Math.Round(Hz, 1)} fps";

        base.Update(gameTime);
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(new Color(0.0f, 0.0f, 0.0f));
        
        InstancedBasicEffect.Parameters["WorldToView"].SetValue(WorldToView);
        InstancedBasicEffect.Parameters["ViewToClip"].SetValue(ViewToClip);

        RasterizerState rasterizerState = new RasterizerState();
        rasterizerState.CullMode = CullMode.CullCounterClockwiseFace;
        GraphicsDevice.RasterizerState = rasterizerState;
        GraphicsDevice.Indices = VoronoiIndexBuffer;

        GraphicsDevice.SetVertexBuffers(
            new VertexBufferBinding(VoronoiVertexBuffer, 0, 0),
            new VertexBufferBinding(SplatBuffer, 0, 1));

        foreach (EffectPass Pass in InstancedBasicEffect.CurrentTechnique.Passes)
        {
            Pass.Apply();
            //GraphicsDevice.DrawPrimitives(PrimitiveType.TriangleList, 0, VertexCount);
            GraphicsDevice.DrawInstancedPrimitives(PrimitiveType.TriangleList, 0, 0, VoronoiVertexCount / 3, Cells);
        }

        base.Draw(gameTime);
    }
}

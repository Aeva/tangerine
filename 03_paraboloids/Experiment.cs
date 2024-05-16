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

namespace HelloVoronoi;


public class Experiment : Game
{
    private GraphicsDeviceManager _graphics;
    private VertexBuffer VoronoiBuffer;
    private int VertexCount = 0;
    private float AspectRatio;

    // The number of subdivisions for the parabola quad like so:
    // 0 -> 0               1
    // 1 -> 0       1       2
    // 2 -> 0   1   2   3   4
    // 3 -> 0 1 2 3 4 5 6 7 8
    private int ParaboloidResolution = 1;

    // Number of voronoi seeds.
    private int Cells = 5000;

    public Experiment()
    {
        _graphics = new GraphicsDeviceManager(this);
        Content.RootDirectory = "Content";
        IsMouseVisible = true;

        _graphics.HardwareModeSwitch = false;
        _graphics.GraphicsProfile = GraphicsProfile.HiDef;

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
        Window.Title = "\"Forward\" Voronoi Diagram Rendering";
        base.Initialize();
    }

    protected override void LoadContent()
    {
        var RNG = new Random(1234);

        int Edges = 1 << ParaboloidResolution;
        var ParaboloidVertices = new Vector3 [Edges * Edges * 6];
        {
            Vector2[] QuadVerts = {
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
                        ParaboloidVertices[Index++] = Position;
                    }
                }
            }
        }

        var UploadData = new VertexPositionColor[ParaboloidVertices.Length * Cells];

        int VertexIndex = 0;
        for (int CellIndex = 0; CellIndex < Cells; ++CellIndex)
        {
            var Offset = new Vector3(
                ((float)RNG.Next(-1000, 1000)) / 900.0f,
                ((float)RNG.Next(-1000, 1000)) / 900.0f * AspectRatio,
                0.0f);
            var Scale = new Vector3(1.0f, 1.0f, 1.0f);

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

            foreach (var Position in ParaboloidVertices)
            {
                UploadData[VertexIndex++] = new VertexPositionColor(Position * Scale + Offset, Fill);
            }
        }

        VertexCount = UploadData.Length;
        VoronoiBuffer = new VertexBuffer(
            GraphicsDevice,
            typeof(VertexPositionColor),
            UploadData.Length,
            BufferUsage.WriteOnly);
        VoronoiBuffer.SetData<VertexPositionColor>(UploadData);
    }

    protected override void Update(GameTime gameTime)
    {
        if (GamePad.GetState(PlayerIndex.One).Buttons.Back == ButtonState.Pressed || Keyboard.GetState().IsKeyDown(Keys.Escape))
        {
            Exit();
        }

        base.Update(gameTime);
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(new Color(0.0f, 0.0f, 0.0f));

        var Shaderz = new BasicEffect(GraphicsDevice);
        Shaderz.World = Matrix.CreateTranslation(0, 0, 0);
        Shaderz.View = Matrix.CreateLookAt(new Vector3(0, 0, 3), new Vector3(0, 0, 0), new Vector3(0, 1, 0));
        Shaderz.Projection = Matrix.CreateOrthographic(2.0f, 2.0f * AspectRatio, 0.001f, 20.0f);
        Shaderz.VertexColorEnabled = true;

        RasterizerState rasterizerState = new RasterizerState();
        rasterizerState.CullMode = CullMode.CullCounterClockwiseFace;
        GraphicsDevice.RasterizerState = rasterizerState;

        GraphicsDevice.SetVertexBuffer(VoronoiBuffer);

        foreach (EffectPass Pass in Shaderz.CurrentTechnique.Passes)
        {
            Pass.Apply();
            GraphicsDevice.DrawPrimitives(PrimitiveType.TriangleList, 0, VertexCount);
        }

        base.Draw(gameTime);
    }
}

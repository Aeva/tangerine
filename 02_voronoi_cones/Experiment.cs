using System;
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
        Random RNG = new Random();

        const int DiscEdgeCount = 3; // increase to adjust apparent metric
        const int DiscVertexCount = DiscEdgeCount * 3;

        var DiscTemplate = new Vector3[DiscVertexCount];
        {
            var DiscPoint = (float Radius, float Degrees) =>
            {
                float Z = -1.0f;
                float Angle = ToRadians(Degrees);
                return new Vector3(
                    (float)System.Math.Sin(Angle) * Radius,
                    (float)System.Math.Cos(Angle) * Radius,
                    Z);
            };

            const float WedgeAngle = 360.0f / (float)DiscEdgeCount;
            const float AngleStart = 180.0f;
            int Index = 0;
            for (int Edge = 0; Edge < DiscEdgeCount; ++Edge)
            {
                float AngleA = ((float)Edge) * WedgeAngle + AngleStart;
                float AngleB = ((float)Edge + 1) * WedgeAngle + AngleStart;
                DiscTemplate[Index++] = DiscPoint(1.0f, AngleA);
                DiscTemplate[Index++] = DiscPoint(1.0f, AngleB);
                DiscTemplate[Index++] = new Vector3(0.0f, 0.0f, 0.0f);
            }
        }

#if true
        const int DiscCount = 500;
#else
        // stress test
        int DiscCount =
            (GraphicsAdapter.DefaultAdapter.CurrentDisplayMode.Width *
            GraphicsAdapter.DefaultAdapter.CurrentDisplayMode.Height) / 32;
#endif
        VertexCount = DiscCount * DiscVertexCount;

        var VertexData = new VertexPositionColor[VertexCount];
        {
            int Index = 0;
            for (int Disc = 0; Disc < DiscCount; ++Disc)
            {
                var Offset = new Vector3(
                    ((float)RNG.Next(-1000, 1000)) / 900.0f,
                    ((float)RNG.Next(-1000, 1000)) / 900.0f * AspectRatio,
                    0.0f);
#if false
                var Color = new Color(RNG.Next(0, 255), RNG.Next(0, 255), RNG.Next(0, 255));
#elif false
                var Distort = new Vector3(
                    (float)System.Math.Sin(Offset.Y * 2) + Offset.X,
                    (float)System.Math.Sin(Offset.X * 4) + Offset.Y,
                    0.0f);
                float Dist = Distort.Length() * 300.0f;
                Dist = System.Math.Abs((Dist % 100.0f) / 50.0f - 1.0f);
                //var Color = new Color(Dist, Dist, Dist);
                var Color = new Color(
                    Lerp((float)RNG.Next(0, 255) / 255.0f * Dist, 1.0f, Dist),
                    Lerp((float)RNG.Next(0, 255) / 255.0f * Dist, 1.0f, Dist),
                    Lerp((float)RNG.Next(0, 255) / 255.0f * Dist, 1.0f, Dist));
#else
                float Dist = Offset.Length();
                Dist *= Dist;
                var Color = new Color(
                    Lerp((float)RNG.Next(0, 255) / 255.0f * Dist, 1.0f, Dist),
                    Lerp((float)RNG.Next(0, 255) / 255.0f * Dist, 1.0f, Dist),
                    Lerp((float)RNG.Next(0, 255) / 255.0f * Dist, 1.0f, Dist));
#endif
                foreach (var Vertex in DiscTemplate)
                {
                    VertexData[Index++] = new (Vertex + Offset, Color);
                }
            }
        }

        VoronoiBuffer = new VertexBuffer(GraphicsDevice, typeof(VertexPositionColor), VertexData.Length, BufferUsage.WriteOnly);
        VoronoiBuffer.SetData<VertexPositionColor>(VertexData);
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
        Shaderz.Projection = Matrix.CreateOrthographic(2.0f, 2.0f * AspectRatio, 0.001f, 10.0f);
        Shaderz.VertexColorEnabled = true;

        RasterizerState rasterizerState = new RasterizerState();
        rasterizerState.CullMode = CullMode.None;
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

using System;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

using Color = Microsoft.Xna.Framework.Color;
using Rectangle = Microsoft.Xna.Framework.Rectangle;
using Vector2 = Microsoft.Xna.Framework.Vector2;
using Vector3 = Microsoft.Xna.Framework.Vector3;

using static Microsoft.Xna.Framework.MathHelper;

namespace HelloTriangle;


public class Experiment : Game
{
    private GraphicsDeviceManager _graphics;
    private SpriteBatch _spriteBatch;

    private VertexBuffer vertexBuffer;
    private IndexBuffer indexBuffer;

    public Experiment()
    {
        _graphics = new GraphicsDeviceManager(this);
        Content.RootDirectory = "Content";
        IsMouseVisible = true;

        _graphics.HardwareModeSwitch = false;
        _graphics.GraphicsProfile = GraphicsProfile.HiDef;

        _graphics.PreferredBackBufferWidth = 600;
        _graphics.PreferredBackBufferHeight = 600;
        _graphics.IsFullScreen = false;
        IsMouseVisible = true;

        _graphics.ApplyChanges();
    }

    protected override void Initialize()
    {
        Window.Title = "Could a depressed person do this?";
        base.Initialize();
    }

    protected override void LoadContent()
    {
        _spriteBatch = new SpriteBatch(GraphicsDevice);

        var VertexData = new VertexPositionColor[3];
        var IndexData = new short[3];

        VertexData[0] = new (new Vector3(0.0f, 0.6f, 0.0f), Color.Red);
        VertexData[1] = new (new Vector3(1.0f, -.5f, 0.0f), Color.Green);
        VertexData[2] = new (new Vector3(-1.0f, -.5f, 0.0f), Color.Blue);

        IndexData[0] = 0;
        IndexData[1] = 1;
        IndexData[2] = 2;

        vertexBuffer = new VertexBuffer(GraphicsDevice, typeof(VertexPositionColor), VertexData.Length, BufferUsage.WriteOnly);
        vertexBuffer.SetData<VertexPositionColor>(VertexData);

        indexBuffer = new IndexBuffer(
            GraphicsDevice,
            IndexElementSize.SixteenBits,
            sizeof(short) * IndexData.Length,
            BufferUsage.WriteOnly);

        indexBuffer.SetData<short>(IndexData);
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
        GraphicsDevice.Clear(new Color(0.05f, 0.05f, 0.05f));

        var Shaderz = new BasicEffect(GraphicsDevice);
        Shaderz.World = Matrix.CreateTranslation(0, 0, 0);
        Shaderz.View = Matrix.CreateLookAt(new Vector3(0, 0, 3), new Vector3(0, 0, 0), new Vector3(0, 1, 0));
        Shaderz.Projection = Matrix.CreatePerspectiveFieldOfView(MathHelper.ToRadians(45), 800f / 480f, 0.01f, 100f);
        Shaderz.VertexColorEnabled = true;

        RasterizerState rasterizerState = new RasterizerState();
        rasterizerState.CullMode = CullMode.None;
        GraphicsDevice.RasterizerState = rasterizerState;

        GraphicsDevice.SetVertexBuffer(vertexBuffer);

        // Both of these work just fine
#if true
        GraphicsDevice.Indices = indexBuffer;
        foreach (EffectPass Pass in Shaderz.CurrentTechnique.Passes)
        {
            Pass.Apply();
            GraphicsDevice.DrawIndexedPrimitives(PrimitiveType.TriangleList, 0, 0, 3);
        }
#else
        foreach (EffectPass Pass in Shaderz.CurrentTechnique.Passes)
        {
            Pass.Apply();
            GraphicsDevice.DrawPrimitives(PrimitiveType.TriangleList, 0, 3);
        }
#endif

        base.Draw(gameTime);
    }
}

using System;

namespace Perf;


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

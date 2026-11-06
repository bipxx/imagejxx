package ij.plugin;

/** Exact sample-count boundary using the maintained actual-command observer. */
public class ObserveInterpolationThreshold {
    public static void main(String[] args) {
        Thread.currentThread().setName("Run$_imagejxx-threshold-reference");
        System.out.print("{\"reference\":\"ImageJ1.54r Interpolate exact 150-sample zoom boundary\",\"cases\":[");
        for(double zoom:new double[]{11.99,12,16})
            for(double interval:new double[]{.625,.63,.635})
                ObserveInterpolationRepeat.sample("dense",zoom,interval,false,false);
        System.out.println("]}");
    }
}

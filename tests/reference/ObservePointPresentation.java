// Original ImageJ 1.54r Graphics2D command observer. No native formulas.
// Run headless against the pinned reviewed ImageJ classes and JDK.
import ij.gui.PointRoi;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.util.ArrayList;
public class ObservePointPresentation {
  static class Trace extends Graphics2D {
    final Graphics2D g;
    final ArrayList<String> rows=new ArrayList<>();
    Trace(){g=new BufferedImage(160,160,BufferedImage.TYPE_INT_ARGB).createGraphics();}
    void record(int kind,int x0,int y0,int x1,int y1,String text){
      Color c=g.getColor();
      float width=(kind==0||kind==3)?((BasicStroke)g.getStroke()).getLineWidth():1;
      int font=kind==4?g.getFont().getSize():0;
      rows.add("{\"kind\":"+kind+",\"coordinates\":["+x0+","+y0+","+x1+","+y1+"],\"color\":["+
        c.getRed()+","+c.getGreen()+","+c.getBlue()+","+c.getAlpha()+"],\"stroke_width\":"+width+
        ",\"font_size\":"+font+",\"text\":\""+text+"\"}");
    }
    @Override public void draw(java.awt.Shape a0){g.draw(a0);}
    @Override public boolean drawImage(java.awt.Image a0,java.awt.geom.AffineTransform a1,java.awt.image.ImageObserver a2){return g.drawImage(a0,a1,a2);}
    @Override public void drawImage(java.awt.image.BufferedImage a0,java.awt.image.BufferedImageOp a1,int a2,int a3){g.drawImage(a0,a1,a2,a3);}
    @Override public void drawRenderedImage(java.awt.image.RenderedImage a0,java.awt.geom.AffineTransform a1){g.drawRenderedImage(a0,a1);}
    @Override public void drawRenderableImage(java.awt.image.renderable.RenderableImage a0,java.awt.geom.AffineTransform a1){g.drawRenderableImage(a0,a1);}
    @Override public void drawString(java.lang.String a0,int a1,int a2){record(4,a1,a2,0,0,a0);g.drawString(a0,a1,a2);}
    @Override public void drawString(java.lang.String a0,float a1,float a2){g.drawString(a0,a1,a2);}
    @Override public void drawString(java.text.AttributedCharacterIterator a0,int a1,int a2){g.drawString(a0,a1,a2);}
    @Override public void drawString(java.text.AttributedCharacterIterator a0,float a1,float a2){g.drawString(a0,a1,a2);}
    @Override public void drawGlyphVector(java.awt.font.GlyphVector a0,float a1,float a2){g.drawGlyphVector(a0,a1,a2);}
    @Override public void fill(java.awt.Shape a0){g.fill(a0);}
    @Override public boolean hit(java.awt.Rectangle a0,java.awt.Shape a1,boolean a2){return g.hit(a0,a1,a2);}
    @Override public java.awt.GraphicsConfiguration getDeviceConfiguration(){return g.getDeviceConfiguration();}
    @Override public void setComposite(java.awt.Composite a0){g.setComposite(a0);}
    @Override public void setPaint(java.awt.Paint a0){g.setPaint(a0);}
    @Override public void setStroke(java.awt.Stroke a0){g.setStroke(a0);}
    @Override public void setRenderingHint(java.awt.RenderingHints.Key a0,java.lang.Object a1){g.setRenderingHint(a0,a1);}
    @Override public java.lang.Object getRenderingHint(java.awt.RenderingHints.Key a0){return g.getRenderingHint(a0);}
    @Override public void setRenderingHints(java.util.Map<?,?> a0){g.setRenderingHints(a0);}
    @Override public void addRenderingHints(java.util.Map<?,?> a0){g.addRenderingHints(a0);}
    @Override public java.awt.RenderingHints getRenderingHints(){return g.getRenderingHints();}
    @Override public void translate(int a0,int a1){g.translate(a0,a1);}
    @Override public void translate(double a0,double a1){g.translate(a0,a1);}
    @Override public void rotate(double a0){g.rotate(a0);}
    @Override public void rotate(double a0,double a1,double a2){g.rotate(a0,a1,a2);}
    @Override public void scale(double a0,double a1){g.scale(a0,a1);}
    @Override public void shear(double a0,double a1){g.shear(a0,a1);}
    @Override public void transform(java.awt.geom.AffineTransform a0){g.transform(a0);}
    @Override public void setTransform(java.awt.geom.AffineTransform a0){g.setTransform(a0);}
    @Override public java.awt.geom.AffineTransform getTransform(){return g.getTransform();}
    @Override public java.awt.Paint getPaint(){return g.getPaint();}
    @Override public java.awt.Composite getComposite(){return g.getComposite();}
    @Override public void setBackground(java.awt.Color a0){g.setBackground(a0);}
    @Override public java.awt.Color getBackground(){return g.getBackground();}
    @Override public java.awt.Stroke getStroke(){return g.getStroke();}
    @Override public void clip(java.awt.Shape a0){g.clip(a0);}
    @Override public java.awt.font.FontRenderContext getFontRenderContext(){return g.getFontRenderContext();}
    @Override public java.awt.Graphics create(){return g.create();}
    @Override public java.awt.Color getColor(){return g.getColor();}
    @Override public void setColor(java.awt.Color a0){g.setColor(a0);}
    @Override public void setPaintMode(){g.setPaintMode();}
    @Override public void setXORMode(java.awt.Color a0){g.setXORMode(a0);}
    @Override public java.awt.Font getFont(){return g.getFont();}
    @Override public void setFont(java.awt.Font a0){g.setFont(a0);}
    @Override public java.awt.FontMetrics getFontMetrics(java.awt.Font a0){return g.getFontMetrics(a0);}
    @Override public java.awt.Rectangle getClipBounds(){return g.getClipBounds();}
    @Override public void clipRect(int a0,int a1,int a2,int a3){g.clipRect(a0,a1,a2,a3);}
    @Override public void setClip(int a0,int a1,int a2,int a3){g.setClip(a0,a1,a2,a3);}
    @Override public java.awt.Shape getClip(){return g.getClip();}
    @Override public void setClip(java.awt.Shape a0){g.setClip(a0);}
    @Override public void copyArea(int a0,int a1,int a2,int a3,int a4,int a5){g.copyArea(a0,a1,a2,a3,a4,a5);}
    @Override public void drawLine(int a0,int a1,int a2,int a3){record(0,a0,a1,a2,a3,"");g.drawLine(a0,a1,a2,a3);}
    @Override public void fillRect(int a0,int a1,int a2,int a3){record(1,a0,a1,a0+a2,a1+a3,"");g.fillRect(a0,a1,a2,a3);}
    @Override public void clearRect(int a0,int a1,int a2,int a3){g.clearRect(a0,a1,a2,a3);}
    @Override public void drawRoundRect(int a0,int a1,int a2,int a3,int a4,int a5){g.drawRoundRect(a0,a1,a2,a3,a4,a5);}
    @Override public void fillRoundRect(int a0,int a1,int a2,int a3,int a4,int a5){g.fillRoundRect(a0,a1,a2,a3,a4,a5);}
    @Override public void drawOval(int a0,int a1,int a2,int a3){record(3,a0,a1,a0+a2,a1+a3,"");g.drawOval(a0,a1,a2,a3);}
    @Override public void fillOval(int a0,int a1,int a2,int a3){record(2,a0,a1,a0+a2,a1+a3,"");g.fillOval(a0,a1,a2,a3);}
    @Override public void drawArc(int a0,int a1,int a2,int a3,int a4,int a5){g.drawArc(a0,a1,a2,a3,a4,a5);}
    @Override public void fillArc(int a0,int a1,int a2,int a3,int a4,int a5){g.fillArc(a0,a1,a2,a3,a4,a5);}
    @Override public void drawPolyline(int[] a0,int[] a1,int a2){g.drawPolyline(a0,a1,a2);}
    @Override public void drawPolygon(int[] a0,int[] a1,int a2){g.drawPolygon(a0,a1,a2);}
    @Override public void fillPolygon(int[] a0,int[] a1,int a2){g.fillPolygon(a0,a1,a2);}
    @Override public boolean drawImage(java.awt.Image a0,int a1,int a2,java.awt.image.ImageObserver a3){return g.drawImage(a0,a1,a2,a3);}
    @Override public boolean drawImage(java.awt.Image a0,int a1,int a2,int a3,int a4,java.awt.image.ImageObserver a5){return g.drawImage(a0,a1,a2,a3,a4,a5);}
    @Override public boolean drawImage(java.awt.Image a0,int a1,int a2,java.awt.Color a3,java.awt.image.ImageObserver a4){return g.drawImage(a0,a1,a2,a3,a4);}
    @Override public boolean drawImage(java.awt.Image a0,int a1,int a2,int a3,int a4,java.awt.Color a5,java.awt.image.ImageObserver a6){return g.drawImage(a0,a1,a2,a3,a4,a5,a6);}
    @Override public boolean drawImage(java.awt.Image a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,java.awt.image.ImageObserver a9){return g.drawImage(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9);}
    @Override public boolean drawImage(java.awt.Image a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,java.awt.Color a9,java.awt.image.ImageObserver a10){return g.drawImage(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10);}
    @Override public void dispose(){g.dispose();}
  }
  public static void main(String[] args) {
    System.setProperty("java.awt.headless","true");
    for(int marker=0;marker<4;marker++)for(int size=0;size<7;size++)
      for(int labels=0;labels<2;labels++)for(int counters=0;counters<2;counters++) {
        PointRoi roi=new PointRoi(new float[]{32.5f,64},new float[]{40.5f,65},2);
        roi.setImage(new ij.ImagePlus("point observer",new ij.process.ByteProcessor(160,160)));
        roi.setPointType(marker);roi.setSize(size);roi.setShowLabels(labels!=0);
        roi.setStrokeColor(new Color(120,40,80,200));
        if(counters!=0)roi.setCounters(new int[]{0,2});
        Trace trace=new Trace();roi.draw(trace);
        System.out.println("{\"marker\":"+marker+",\"size\":"+size+",\"labels\":"+(labels!=0)+
          ",\"counters\":"+(counters!=0)+",\"commands\":["+String.join(",",trace.rows)+"]}");
        trace.dispose();
      }
  }
}

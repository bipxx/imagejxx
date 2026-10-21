"""Observe fixed ImageJ 1.54r ImageCanvas creation events in excluded tooling.

Prepare compiles an independently authored driver and probes hidden AWT wiring.
Capture is a separate explicit action, permitted only after root freezes Q01-Q17.
No Java source/runtime is incorporated in the product; no expected masks, parity
decisions, commercial clearance or OS desktop-input claims are produced here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
import capture_roi_boolean_reference as reference

ROOT = reference.ROOT
STAGE = ROOT / "build/references/selection-creation-imagej154r-001"
CONTRACT = "p05l.selection-creation-reference.001"
RECEIPT = ROOT / "build/p06c-001/channel-display-entry-001.json"
RECEIPT_SHA = "5360efa8a9f605d845aec107d6d9e1ee940e9732d0bdafdbd07a4c213d1bb471"
REUSED_CLASSES = ROOT / "build/p06c-001/capture-001/classes"
CLASS_NAME = "SelectionCreationReference"
CASE_IDS = tuple(f"Q{number:02d}" for number in range(1, 18))
SOURCE_PINS = {
    **reference.SOURCE_PINS,
    "ij/gui/ImageCanvas.java": "ab27a57aa060ca8e297d59b09e478406ebd7a3bbd5d6a741bc1ab56c7cc553ab",
    "ij/gui/ImageWindow.java": "ca2cf35410411d8b53a02747ad2a5314598b9e79d201c9e632fef4b7aef5810b",
    "ij/gui/StackWindow.java": "333063c0a94206e656c1a67d72e7482397336644c911fc659f73539067146b57",
    "ij/gui/FreehandRoi.java": "c190ec16c3b027b50906d08926fc6ee7b8c0f6845e974b3a974ecc8c3accaba8",
    "ij/gui/Line.java": "49aa00dd002627b17b4a287927b5c198a7ee136a788284d6850dccff95ddb67e",
    "ij/gui/PointRoi.java": "f77398d2822dc2235c06c4340f84ed4a2a7d8d5b7d573fe2870325990a16fd77",
    "ij/gui/Toolbar.java": "baa59de05416ca3bd574f43aff67b86902a3c5dd0579a3ece95515f07f453c14",
    "ij/ImagePlus.java": "477e3f5f913b9c42feb68ea35d21836d45fd56920cccbfc5719a06822cbcfab9",
    "ij/ImageJ.java": "e043a86f66cbb32c2ab2866502b0e37436b639171697ee706bcab92219d26a59",
}


def cases() -> list[dict]:
    """Only fixed inputs; this function computes no reference expectations."""
    rows = []
    for number in range(1, 18):
        base = {"kind": "rectangle", "xywh": [4, 4, 10, 8]}
        if number in (7, 8):
            base = None
        elif number == 15:
            base = {"kind": "line", "points": [[4, 4], [14, 12]]}
        elif number == 16:
            base = {"kind": "point", "points": [[8, 7]]}
        tool = "ellipse" if number == 9 else "polygon" if number == 12 else "freehand" if number == 13 else "rectangle"
        events, held = [], set()

        def key(name: str, down: bool):
            if down:
                held.add(name)
            else:
                held.discard(name)
            events.append({"kind": "key", "label": name + (".down" if down else ".up"),
                           "name": name, "down": down, "modifiers": sorted(held - {"escape"})})

        def mouse(action: str, x: int, y: int, label: str | None = None):
            events.append({"kind": "mouse", "label": label or action,
                           "action": action, "image_point": [x, y],
                           "button": "none" if action == "move" else "left",
                           "modifiers": sorted(held - {"escape"}), "click_count": 1})

        def drag(start=(18, 6), middle=(21, 8), end=(25, 10)):
            mouse("press", *start)
            mouse("drag", *middle, "drag.1")
            mouse("drag", *end, "drag.2")
            mouse("release", *end)

        if number in (2, 4, 7, 9, 15, 17):
            key("shift", True)
        if number in (3, 4, 8, 13, 16):
            key("alt", True)
        if number == 17:
            key("ctrl", True)
        if number in (1, 2, 4, 9, 15, 17):
            drag()
        elif number == 3:
            drag((8, 7), (10, 8), (12, 10))
        elif number in (5, 6):
            if number == 5:
                key("shift", True)
            mouse("press", 18, 6)
            mouse("drag", 21, 8, "drag.1")
            key("shift", number == 6)
            mouse("drag", 25, 10, "drag.2")
            mouse("release", 25, 10)
        elif number in (7, 8):
            drag((10, 7), (13, 9), (17, 11))
        elif number == 10:
            mouse("press", 18, 6)
            mouse("drag", 21, 8, "drag.1")
            mouse("release", 25, 10)
        elif number == 11:
            key("shift", True)
            mouse("press", 18, 6)
            mouse("drag", 25, 10, "drag.1")
            key("escape", True)
            key("escape", False)
            mouse("release", 25, 10)
        elif number == 12:
            key("shift", True)
            mouse("press", 18, 4, "vertex.1.press")
            mouse("release", 18, 4, "vertex.1.release")
            key("shift", False)
            for index, (x, y) in enumerate(((25, 4), (25, 11), (18, 11)), 2):
                if index == 3:
                    key("alt", True)
                if index == 4:
                    key("alt", False)
                for action in ("move", "press", "release"):
                    mouse(action, x, y, f"vertex.{index}.{action}")
            for action in ("move", "press", "release"):
                mouse(action, 18, 4, "finish." + action)
        elif number == 13:
            mouse("press", 8, 7)
            for index, point in enumerate(((12, 7), (12, 10), (8, 10), (8, 7)), 1):
                mouse("drag", *point, f"drag.{index}")
            mouse("release", 8, 7)
        elif number == 14:
            key("shift", True)
            mouse("press", 4, 4, "handle.press")
            mouse("drag", 7, 6, "handle.drag.1")
            mouse("release", 7, 6, "handle.release")
            key("shift", False)
            events.append({"kind": "setup", "label": "baseline.reset", "method": "explicit_api_baseline_reset"})
            key("alt", True)
            mouse("press", 8, 7, "interior.press")
            mouse("drag", 12, 10, "interior.drag.1")
            mouse("release", 12, 10, "interior.release")
        elif number == 16:
            mouse("press", 6, 5)
            mouse("drag", 12, 10, "drag.1")
            mouse("release", 12, 10)
        for name in sorted(held):
            key(name, False)
        rows.append({"id": f"Q{number:02d}", "tool": tool, "base": base,
                     "canvas_wh": [32, 24], "source_shape": [2, 24, 32],
                     "slice_index": 1, "magnification": 8,
                     "source_rect_xywh": [0, 0, 32, 24], "events": events})
    if tuple(row["id"] for row in rows) != CASE_IDS:
        raise ValueError("Internal fixed input membership differs")
    return rows


DRIVER = r'''import ij.IJ;
import ij.ImageJ;
import ij.ImagePlus;
import ij.ImageStack;
import ij.Prefs;
import ij.WindowManager;
import ij.gui.ImageCanvas;
import ij.gui.StackWindow;
import ij.gui.Line;
import ij.gui.PointRoi;
import ij.gui.Roi;
import ij.gui.ShapeRoi;
import ij.gui.Toolbar;
import ij.process.ByteProcessor;
import ij.process.FloatPolygon;
import ij.process.ImageProcessor;
import java.awt.EventQueue;
import java.awt.Component;
import java.awt.Container;
import java.awt.Frame;
import java.awt.Graphics2D;
import java.awt.GraphicsEnvironment;
import java.awt.Rectangle;
import java.awt.event.InputEvent;
import java.awt.event.KeyEvent;
import java.awt.event.KeyListener;
import java.awt.event.MouseEvent;
import java.awt.geom.Rectangle2D;
import java.awt.image.BufferedImage;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

public final class SelectionCreationReference {
    static final int W=32,H=24,Z=2,MAG=8;
    static final String CONTRACT="p05l.selection-creation-reference.001";
    static final List<Object> CASES=__CASES__;
    static ImageJ app; static ImagePlus image; static ImageCanvas canvas;
    static HiddenWindow window; static BufferedImage surface;
    static Roi candidate,base; static int paints=0;
    static final Field MOD;
    static {try {MOD=Roi.class.getDeclaredField("modState");MOD.setAccessible(true);}catch(Exception e){throw new ExceptionInInitializerError(e);}}
    // ImagePlus.updateAndDraw replaces a plain ImageWindow for a stack and
    // resets its slice. Use the real stack host while suppressing presentation.
    static final class HiddenWindow extends StackWindow {
        HiddenWindow(ImagePlus value,ImageCanvas target){super(value,target);}
        @Override public void show() {} // Only presentation is suppressed.
        @Override public void setVisible(boolean value){super.setVisible(false);}
    }
    static Map<String,Object> o(Object...pairs){
        Map<String,Object> out=new LinkedHashMap<>();
        for(int i=0;i<pairs.length;i+=2)out.put((String)pairs[i],pairs[i+1]);return out;
    }
    static List<Object> a(Object...items){return new ArrayList<>(Arrays.asList(items));}
    @SuppressWarnings("unchecked") static Map<String,Object> map(Object x){return (Map<String,Object>)x;}
    @SuppressWarnings("unchecked") static List<Object> list(Object x){return (List<Object>)x;}
    static int number(Object x){return ((Number)x).intValue();}
    static String quote(String value){StringBuilder b=new StringBuilder("\"");
        for(int i=0;i<value.length();++i){char c=value.charAt(i);if(c=='"'||c=='\\')b.append('\\').append(c);
            else if(c<32)b.append(String.format("\\u%04x",(int)c));else b.append(c);}return b.append('"').toString();}
    static String json(Object value){
        if(value==null)return "null";if(value instanceof String)return quote((String)value);
        if(value instanceof Number||value instanceof Boolean)return value.toString();
        StringBuilder b=new StringBuilder();
        if(value instanceof Map){b.append('{');boolean first=true;for(Object entry:((Map<?,?>)value).entrySet()){
            Map.Entry<?,?> e=(Map.Entry<?,?>)entry;if(!first)b.append(',');first=false;b.append(quote((String)e.getKey())).append(':').append(json(e.getValue()));}return b.append('}').toString();}
        if(value instanceof Iterable){b.append('[');boolean first=true;for(Object item:(Iterable<?>)value){if(!first)b.append(',');first=false;b.append(json(item));}return b.append(']').toString();}
        if(value.getClass().isArray()){b.append('[');for(int i=0;i<Array.getLength(value);++i){if(i>0)b.append(',');b.append(json(Array.get(value,i)));}return b.append(']').toString();}
        throw new IllegalArgumentException("Unsupported observation value "+value.getClass());
    }
    static String hex(byte[] bytes){StringBuilder b=new StringBuilder(bytes.length*2);for(byte v:bytes)b.append(String.format("%02x",v&255));return b.toString();}
    static List<String> rows(ImageProcessor pixels){List<String> out=new ArrayList<>();
        for(int y=0;y<H;++y){StringBuilder row=new StringBuilder();for(int x=0;x<W;++x){int value=pixels.get(x,y);if(value!=0&&value!=255)throw new IllegalStateException("Nonbinary observed reference fill");row.append(value==0?'0':'1');}out.add(row.toString());}return out;}
    static Map<String,Object> source(){byte[] out=new byte[W*H*Z];
        for(int z=0;z<Z;++z)System.arraycopy((byte[])image.getStack().getPixels(z+1),0,out,z*W*H,W*H);
        return o("shape",a(Z,H,W),"dtype","uint8","pixels_hex",hex(out));}
    static Object roi(Roi actual)throws Exception{
        if(actual==null)return null;
        Rectangle b=actual.getBounds();Rectangle2D f=actual.getFloatBounds();FloatPolygon p=actual.getFloatPolygon();
        List<Object> points=new ArrayList<>();for(int i=0;i<p.npoints;++i)points.add(a(p.xpoints[i],p.ypoints[i]));
        Map<String,Object> out=o("identity",System.identityHashCode(actual),"class",actual.getClass().getName(),
            "type",actual.getType(),"type_name",actual.getTypeAsString(),"state",actual.getState(),"mod_state",MOD.getInt(actual),
            "image_id",actual.getImage()==null?null:actual.getImage().getID(),"is_area",actual.isArea(),"is_line",actual.isLine(),
            "bounds_xywh",a(b.x,b.y,b.width,b.height),"float_bounds_xywh",a(f.getX(),f.getY(),f.getWidth(),f.getHeight()),
            "float_polygon_xy",points,"shape_array",actual instanceof ShapeRoi?((ShapeRoi)actual).getShapeAsArray():null,
            "stroke_width",actual.getStrokeWidth(),"name",actual.getName(),"position",actual.getPosition(),
            "czt_position",a(actual.getCPosition(),actual.getZPosition(),actual.getTPosition()),
            "mask_observation_reference","clone_of_actual_roi","mask_method","ByteProcessor.fill(Roi)");
        // Local-mask construction can cache data. Never call it on a live
        // constructing candidate, retained previous ROI or source processor.
        try {Roi copy=(Roi)actual.clone();ImageProcessor local=copy.getMask();
            if(local==null)out.put("local_mask",null);
            else {byte[] bytes=new byte[local.getWidth()*local.getHeight()];for(int i=0;i<bytes.length;++i)bytes[i]=(byte)local.get(i);
                out.put("local_mask",o("shape",a(local.getHeight(),local.getWidth()),"pixels_hex",hex(bytes)));}
            out.put("local_mask_error",null);
        }catch(RuntimeException e){out.put("local_mask",null);out.put("local_mask_error",e.getClass().getName()+": "+e.getMessage());}
        try {Roi copy=(Roi)actual.clone();ByteProcessor mask=new ByteProcessor(W,H);mask.setColor(255);mask.fill(copy);
            out.put("mask_rows",rows(mask));out.put("mask_pixels_hex",hex((byte[])mask.getPixels()));out.put("mask_error",null);
        }catch(RuntimeException e){out.put("mask_rows",null);out.put("mask_pixels_hex",null);out.put("mask_error",e.getClass().getName()+": "+e.getMessage());}
        return out;
    }
    static void paint(){Graphics2D g=surface.createGraphics();try{canvas.paint(g);++paints;}finally{g.dispose();}}
    static int canvasCount(Component component){int count=component instanceof ImageCanvas?1:0;
        if(component instanceof Container)for(Component child:((Container)component).getComponents())count+=canvasCount(child);return count;}
    static int canvasCount(){int count=0;for(Frame frame:Frame.getFrames())count+=canvasCount(frame);return count;}
    static Map<String,Object> snapshot()throws Exception{
        if(image.getWindow()!=window||window.getCanvas()!=canvas)
            throw new IllegalStateException("Reference image/canvas was retargeted to another window");
        if(image.getWindow().isVisible()||image.getWindow().isShowing()||app.isVisible()||canvasCount()!=1)
            throw new IllegalStateException("Actual reference window visibility/canvas component count differs from the hidden single-canvas contract");
        Roi current=image.getRoi(),previous=Roi.getPreviousRoi();Rectangle r=canvas.getSrcRect();
        return o("current",roi(current),"candidate",roi(candidate),"previous",roi(previous),"base",roi(base),
            "relations",o("current_is_candidate",current!=null&&current==candidate,"current_is_base",current!=null&&current==base,
                "previous_is_base",previous!=null&&previous==base,"current_is_previous",current!=null&&current==previous),
            "keys",o("shift",IJ.shiftKeyDown(),"alt",IJ.altKeyDown(),"ctrl",IJ.controlKeyDown(),"escape",IJ.escapePressed()),
            "tool",o("id",Toolbar.getToolId(),"name",Toolbar.getToolName()),
            "image",o("id",image.getID(),"slice_index",image.getCurrentSlice()-1,"stack_size",image.getStackSize()),
            "canvas",o("magnification",canvas.getMagnification(),"source_rect_xywh",a(r.x,r.y,r.width,r.height),
                "drawing_wh",a(canvas.getWidth(),canvas.getHeight()),"visible",canvas.isVisible(),"showing",canvas.isShowing(),
                "displayable",canvas.isDisplayable(),"paint_count",paints),
            "imagej_visible",app.isVisible(),"window_visible",window.isVisible(),"window_showing",window.isShowing());
    }
    static int keyCode(String name){switch(name){case "shift":return KeyEvent.VK_SHIFT;case "alt":return KeyEvent.VK_ALT;
        case "ctrl":return KeyEvent.VK_CONTROL;case "escape":return KeyEvent.VK_ESCAPE;default:throw new IllegalArgumentException(name);}}
    static int modifiers(Map<String,Object> step){int result=0;for(Object name:list(step.get("modifiers"))){
        if(name.equals("shift"))result|=InputEvent.SHIFT_DOWN_MASK;else if(name.equals("alt"))result|=InputEvent.ALT_DOWN_MASK;
        else if(name.equals("ctrl"))result|=InputEvent.CTRL_DOWN_MASK;else throw new IllegalArgumentException("Unknown fixed modifier");}return result;}
    static void releaseKeys(){for(int code:new int[]{KeyEvent.VK_SHIFT,KeyEvent.VK_ALT,KeyEvent.VK_CONTROL,KeyEvent.VK_META,KeyEvent.VK_ESCAPE}){
        KeyEvent event=new KeyEvent(canvas,KeyEvent.KEY_RELEASED,System.currentTimeMillis(),0,code,KeyEvent.CHAR_UNDEFINED);
        for(KeyListener listener:canvas.getKeyListeners())listener.keyReleased(event);}IJ.resetEscape();}
    static void setup(Map<String,Object> spec){releaseKeys();image.deleteRoi();Roi.setPreviousRoi(null);
        // setSourceRect derives a new magnification from the old destination
        // width. Establish both rectangles first, then set the actual scale.
        image.setSlice(2);canvas.setSourceRect(new Rectangle(0,0,W,H));canvas.setDrawingSize(W*MAG,H*MAG);canvas.setMagnification(MAG);
        String tool=(String)spec.get("tool");Toolbar.getInstance().setTool(tool.equals("ellipse")?Toolbar.OVAL:tool.equals("polygon")?Toolbar.POLYGON:tool.equals("freehand")?Toolbar.FREEROI:Toolbar.RECTANGLE);
        candidate=null;base=null;Object definition=spec.get("base");
        if(definition!=null){Map<String,Object> value=map(definition);String kind=(String)value.get("kind");
            if(kind.equals("rectangle")){List<Object> box=list(value.get("xywh"));base=new Roi(number(box.get(0)),number(box.get(1)),number(box.get(2)),number(box.get(3)));}
            else {List<Object> points=list(value.get("points")),p=list(points.get(0));
                if(kind.equals("point"))base=new PointRoi(number(p.get(0)),number(p.get(1)));
                else {List<Object> q=list(points.get(1));base=new Line(number(p.get(0)),number(p.get(1)),number(q.get(0)),number(q.get(1)));}}
            image.setRoi(base);}
        Roi.setPreviousRoi(null);WindowManager.setCurrentWindow(window);WindowManager.setTempCurrentImage(image);paint();
    }
    static Map<String,Object> deliver(Map<String,Object> spec,Map<String,Object> step)throws Exception{
        String kind=(String)step.get("kind");
        if(kind.equals("setup")){setup(spec);return o("delivery","explicit_api_baseline_reset_not_gesture");}
        int flags=modifiers(step);long when=System.currentTimeMillis();Map<String,Object> delivered;
        if(kind.equals("key")){boolean down=(Boolean)step.get("down");int code=keyCode((String)step.get("name"));
            KeyEvent event=new KeyEvent(canvas,down?KeyEvent.KEY_PRESSED:KeyEvent.KEY_RELEASED,when,flags,code,KeyEvent.CHAR_UNDEFINED);
            List<Object> listeners=new ArrayList<>();for(KeyListener listener:canvas.getKeyListeners()){
                listeners.add(listener.getClass().getName());if(down)listener.keyPressed(event);else listener.keyReleased(event);}
            delivered=o("delivery","registered_ImageJ_KeyListener_on_EDT_not_OS_focus_dispatch","id",event.getID(),"when_ms",event.getWhen(),
                "key_code",event.getKeyCode(),"modifiers",event.getModifiers(),"modifiers_ex",event.getModifiersEx(),"listeners",listeners,"consumed",event.isConsumed());
        }else {String action=(String)step.get("action");List<Object> point=list(step.get("image_point"));int x=number(point.get(0))*MAG,y=number(point.get(1))*MAG;
            int id=action.equals("press")?MouseEvent.MOUSE_PRESSED:action.equals("drag")?MouseEvent.MOUSE_DRAGGED:action.equals("release")?MouseEvent.MOUSE_RELEASED:MouseEvent.MOUSE_MOVED;
            if(action.equals("press")||action.equals("drag"))flags|=InputEvent.BUTTON1_DOWN_MASK;
            int button=action.equals("move")||action.equals("drag")?MouseEvent.NOBUTTON:MouseEvent.BUTTON1;
            MouseEvent event=new MouseEvent(canvas,id,when,flags,x,y,number(step.get("click_count")),false,button);
            canvas.dispatchEvent(event);
            if(action.equals("press")&&candidate==null)candidate=image.getRoi();
            delivered=o("delivery","ImageCanvas.dispatchEvent_on_EDT","id",event.getID(),"when_ms",event.getWhen(),"screen_point",a(x,y),
                "offscreen_point",a(canvas.offScreenX(x),canvas.offScreenY(y)),"button",event.getButton(),"click_count",event.getClickCount(),
                "modifiers",event.getModifiers(),"modifiers_ex",event.getModifiersEx(),"consumed",event.isConsumed());
        }
        paint();return delivered;
    }
    static Map<String,Object> environment(){return o("imagej_version",IJ.getVersion(),"java_version",System.getProperty("java.version"),
        "java_vendor",System.getProperty("java.vendor"),"os_name",System.getProperty("os.name"),"os_arch",System.getProperty("os.arch"),
        "awt_headless",GraphicsEnvironment.isHeadless(),"event_dispatch_thread",EventQueue.isDispatchThread(),"gui_scale",Prefs.getGuiScale(),
        "canvas_count",canvasCount(),"delivery_scope","hidden_NO_SHOW_AWT_handlers_and_offscreen_ImageCanvas.paint_not_OS_UI",
        "pixel_recipe","17*z+3*y+x","brush_size",Toolbar.getBrushSize(),"line_width",Line.getWidth(),"corner_diameter",Toolbar.getRoundRectArcSize());}
    static void init(){
        if(GraphicsEnvironment.isHeadless())throw new IllegalStateException("AWT nonheadless environment required; no headless mask substitute");
        if(!IJ.getVersion().equals("1.54r"))throw new IllegalStateException("Unexpected ImageJ version");
        app=new ImageJ(ImageJ.NO_SHOW);Toolbar.setRoundRectArcSize(0);Toolbar.setBrushSize(0);Line.setWidth(1);Roi.setDefaultStrokeWidth(1);
        ImageStack stack=new ImageStack(W,H);for(int z=0;z<Z;++z){byte[] p=new byte[W*H];for(int y=0;y<H;++y)for(int x=0;x<W;++x)p[y*W+x]=(byte)(17*z+3*y+x);stack.addSlice("z"+z,new ByteProcessor(W,H,p,null));}
        image=new ImagePlus("P05l hidden reference canvas",stack);canvas=new ImageCanvas(image);window=new HiddenWindow(image,canvas);
        surface=new BufferedImage(W*MAG,H*MAG,BufferedImage.TYPE_INT_RGB);WindowManager.setCurrentWindow(window);WindowManager.setTempCurrentImage(image);
    }
    static Map<String,Object> capture(Map<String,Object> spec)throws Exception{
        long start=System.nanoTime();setup(spec);Object initial=snapshot(),before=source();List<Object> events=new ArrayList<>();int index=0;
        for(Object raw:list(spec.get("events"))){Map<String,Object> step=map(raw);Object delivered=deliver(spec,step);
            events.add(o("ordinal",index++,"label",step.get("label"),"input",step,"delivered",delivered,"after",snapshot()));}
        return o("contract",CONTRACT,"id",spec.get("id"),"input_contract",spec,"initial",initial,"events",events,"final",snapshot(),
            "source_before",before,"source_after",source(),"elapsed_ms",(System.nanoTime()-start)/1000000.0,"environment",environment());
    }
    static <T>T edt(Callable<T> work,int seconds)throws Exception{FutureTask<T> task=new FutureTask<>(work);EventQueue.invokeLater(task);return task.get(seconds,TimeUnit.SECONDS);}
    public static void main(String[] args){try{
        if(args.length!=1||!(args[0].equals("--probe")||args[0].equals("--capture")))throw new IllegalArgumentException("Explicit --probe or --capture required");
        edt(()->{init();return null;},10);
        if(args[0].equals("--probe")){System.out.println(json(edt(()->{setup(map(CASES.get(0)));return o("contract",CONTRACT,"kind","environment_probe_not_Q_capture","environment",environment(),"snapshot",snapshot());},5)));}
        else for(Object raw:CASES)System.out.println(json(edt(()->capture(map(raw)),5)));
        edt(()->{releaseKeys();image.changes=false;window.dispose();app.dispose();return null;},5);System.exit(0);
    }catch(Throwable error){error.printStackTrace(System.err);System.exit(1);}}
}
'''


def java_value(value):
    if value is None:
        return "null"
    if type(value) is bool:
        return "true" if value else "false"
    if type(value) is int:
        return str(value)
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=True)
    if isinstance(value, list):
        return "a(" + ",".join(java_value(item) for item in value) + ")"
    if isinstance(value, dict):
        return "o(" + ",".join(java_value(part) for pair in value.items() for part in pair) + ")"
    raise TypeError(type(value))


def pin(path: Path) -> dict:
    return {"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size, "sha256": reference.digest(path)}


def stage_path(path: Path) -> Path:
    resolved = reference._reference_path(path)
    if resolved == STAGE.resolve() or not resolved.is_relative_to(STAGE.resolve()):
        raise ValueError("Selection capture outputs must be fresh children of the owned stage")
    return resolved


def inputs() -> dict:
    downloads = ROOT / "build/references/downloads"
    archives = ((downloads / f"imagej-{reference.COMMIT}.zip", 6146010, reference.IMAGEJ_SHA),
                (downloads / "fiji-latest-20261007-1617-win64-jdk.zip", 705099776, reference.FIJI_SHA))
    for path, size, sha in archives:
        if path.stat().st_size != size:
            raise ValueError("Pinned reference archive size differs")
        reference.verified(reference._BoundedFile(path, size, time.monotonic() + 180), sha)
    sources, runtime = reference.REUSE / "sources", reference.REUSE / "jdk"
    source_members = reference._verify_tree(archives[0][0], f"ImageJ-{reference.COMMIT}/", sources, reference.SOURCE_LIMIT)
    runtime_members = reference._verify_tree(archives[1][0], reference.JDK_PREFIX, runtime, reference.JDK_LIMIT)
    for name, sha in SOURCE_PINS.items():
        if name not in source_members or source_members[name]["sha256"] != sha:
            raise ValueError("Pinned creation source member differs: " + name)
    if reference.digest(RECEIPT) != RECEIPT_SHA:
        raise ValueError("Qualified reference class receipt changed")
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    if receipt["reference"] != {"version": "1.54r", "commit": reference.COMMIT,
                                "source_archive_sha256": reference.IMAGEJ_SHA,
                                "runtime_archive_sha256": reference.FIJI_SHA}:
        raise ValueError("Reused class source/runtime binding differs")
    classes = reference._files(REUSED_CLASSES, reference.MAX_BUILD)
    if classes != receipt["compiled_classes"] or source_members != receipt["verified_source_tree"] or runtime_members != receipt["verified_runtime_tree"]:
        raise ValueError("Reused ImageJ classes/source/JDK differ from their qualified reference receipt")
    return {"archives": [pin(path) for path, _, _ in archives], "source_tree": source_members,
            "runtime_tree": runtime_members, "reused_classes": classes, "reused_class_receipt": pin(RECEIPT),
            "known_source_members": {name: source_members[name] for name in SOURCE_PINS}}


def write_json(path: Path, value: dict) -> None:
    encoded = (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    if len(encoded) > reference.MAX_OUTPUT:
        raise ValueError("Reference report exceeds its declared output bound")
    with path.open("xb") as stream:
        stream.write(encoded)


def command(stage: Path, prepared: Path, mode: str) -> list[str]:
    return [str(reference.REUSE / "jdk/bin/java.exe"), "-Xmx256m", "-Djava.awt.headless=false",
            "-Duser.home=" + str(stage / "home"), "-Djava.io.tmpdir=" + str(stage / "tmp"),
            "-Dij.dir=" + str(stage), "-cp", os.pathsep.join((str(prepared / "classes"), str(REUSED_CLASSES))),
            CLASS_NAME, mode]


def prepare(stage: Path) -> dict:
    stage = stage_path(stage)
    if stage.exists():
        raise ValueError("Preparation directory must be fresh; preserve prior attempts")
    verified = inputs()
    stage.mkdir(parents=True, exist_ok=False)
    for name in ("classes", "home", "tmp"):
        (stage / name).mkdir()
    plan = {"contract": CONTRACT, "ordered_case_ids": list(CASE_IDS), "cases": cases(),
            "bounds": {"canvas_wh": [32, 24], "source_shape": [2, 24, 32], "canvases": 1,
                       "case_seconds": 5, "capture_parent_seconds": 120, "probe_seconds": 20,
                       "compile_seconds": 60, "java_heap_mib": 256,
                       "output_bytes": reference.MAX_OUTPUT, "build_bytes": reference.MAX_BUILD},
            "scope": "hidden AWT ImageCanvas dispatch and registered ImageJ KeyListener with actual offscreen canvas.paint; not OS desktop UI or product acceptance"}
    write_json(stage / "input-plan.json", plan)
    driver = stage / (CLASS_NAME + ".java")
    driver.write_bytes(DRIVER.replace("__CASES__", java_value(cases())).encode("utf-8"))
    compiled = reference._run([str(reference.REUSE / "jdk/bin/javac.exe"), "-J-Xmx512m", "-encoding", "UTF-8", "-proc:none",
                               "-classpath", str(REUSED_CLASSES), "-d", str(stage / "classes"), str(driver)], stage, "javac", 60)
    version = reference._run([str(reference.REUSE / "jdk/bin/java.exe"), "-Xmx128m", "-version"], stage, "version", 10)
    probe = reference._run(command(stage, stage, "--probe"), stage, "probe", 20)
    raw = (stage / "probe.stdout").read_bytes()
    if len(raw) > 128 * 1024:
        raise ValueError("Environment-only probe exceeded its small output bound")
    observed = json.loads(raw)
    if observed.get("kind") != "environment_probe_not_Q_capture" or observed.get("contract") != CONTRACT:
        raise ValueError("Probe emitted an unexpected protocol")
    env, snapshot = observed["environment"], observed["snapshot"]
    if env["imagej_version"] != "1.54r" or env["awt_headless"] or not env["event_dispatch_thread"] or env["canvas_count"] != 1:
        raise ValueError("Actual AWT environment did not satisfy the declared probe")
    if snapshot["imagej_visible"] or snapshot["window_visible"] or snapshot["window_showing"]:
        raise ValueError("Reference probe made an OS window visible")
    canvas, image = snapshot["canvas"], snapshot["image"]
    if (canvas["magnification"] != 8 or canvas["source_rect_xywh"] != [0, 0, 32, 24]
            or canvas["drawing_wh"] != [256, 192] or canvas["paint_count"] != 1
            or image["slice_index"] != 1 or image["stack_size"] != 2):
        raise ValueError("Actual reference canvas scale/source/drawing/slice bounds differ from the frozen input contract")
    if (env["gui_scale"] != 1 or env["brush_size"] != 0
            or env["line_width"] != 1 or env["corner_diameter"] != 0):
        raise ValueError("Actual reference tool settings differ from the frozen input contract")
    after = inputs()
    if verified != after:
        raise ValueError("Reference inputs changed during preparation")
    report = {"contract": CONTRACT, "state": "prepared_environment_probe_only_full17_capture_not_executed",
              "harness": pin(Path(__file__).resolve()), "helper": pin(Path(reference.__file__).resolve()),
              "input_plan": pin(stage / "input-plan.json"), "driver": pin(driver),
              "driver_classes": reference._files(stage / "classes", reference.MAX_BUILD), "inputs": verified,
              "processes": {"compile": compiled, "version": version, "probe": probe}, "probe_observation": observed}
    write_json(stage / "prepared.json", report)
    reference._storage(stage)
    return report


def capture(stage: Path, prepared: Path) -> dict:
    stage, prepared = stage_path(stage), stage_path(prepared)
    if stage.exists():
        raise ValueError("Capture directory must be fresh; retain original failures")
    held = json.loads((prepared / "prepared.json").read_text(encoding="utf-8"))
    if held["harness"] != pin(Path(__file__).resolve()) or held["helper"] != pin(Path(reference.__file__).resolve()):
        raise ValueError("Prepared tools changed; create a fresh preparation")
    for name, item in (("input-plan.json", held["input_plan"]), (CLASS_NAME + ".java", held["driver"])):
        if pin(prepared / name) != item:
            raise ValueError("Prepared source/input contract changed")
    plan = json.loads((prepared / "input-plan.json").read_text(encoding="utf-8"))
    if plan["cases"] != cases() or plan["ordered_case_ids"] != list(CASE_IDS):
        raise ValueError("Frozen fixed17 input membership changed")
    if reference._files(prepared / "classes", reference.MAX_BUILD) != held["driver_classes"] or inputs() != held["inputs"]:
        raise ValueError("Prepared class/source/runtime inputs changed")
    stage.mkdir(parents=True, exist_ok=False)
    (stage / "home").mkdir(); (stage / "tmp").mkdir()
    process = reference._run(command(stage, prepared, "--capture"), stage, "reference", 120)
    raw = stage / "reference.stdout"
    values = [json.loads(line) for line in raw.read_text(encoding="utf-8").splitlines() if line.strip()]
    if tuple(value.get("id") for value in values) != CASE_IDS:
        raise ValueError("Reference did not emit the exact ordered17 cases")
    for value, spec in zip(values, cases()):
        if value["contract"] != CONTRACT or value["input_contract"] != spec or len(value["events"]) != len(spec["events"]):
            raise ValueError("Reference input observations differ from the frozen protocol")
        if [entry["input"] for entry in value["events"]] != spec["events"]:
            raise ValueError("Reference event observations omit or alter a declared input")
        if value["environment"]["canvas_count"] != 1 or value["elapsed_ms"] > 5000:
            raise ValueError("Reference exceeded a per-case input/time bound")
    if inputs() != held["inputs"]:
        raise ValueError("Reference inputs changed during execution")
    report = {"contract": CONTRACT, "state": "raw_reference_observations_not_product_or_parity_acceptance",
              "prepared": pin(prepared / "prepared.json"), "raw": pin(raw), "process": process,
              "harness": pin(Path(__file__).resolve()), "ordered_case_ids": list(CASE_IDS),
              "case_count": len(values), "input_plan": held["input_plan"],
              "scope": plan["scope"], "commercial_eligibility": "not_decided_by_this_tool"}
    write_json(stage / "capture.json", report)
    reference._storage(stage)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--prepare", action="store_true", help="Compile and run environment-only wiring probe; never Q01-Q17")
    action.add_argument("--capture", action="store_true", help="Explicit full17 execution after independent/root contract freeze")
    parser.add_argument("--stage", type=Path)
    parser.add_argument("--prepared", type=Path, default=STAGE / "prepare-001")
    args = parser.parse_args()
    stage = stage_path(args.stage or STAGE / ("prepare-001" if args.prepare else "capture-001"))
    report = prepare(stage) if args.prepare else capture(stage, args.prepared)
    print(json.dumps({"state": report["state"], "stage": stage.relative_to(ROOT).as_posix(),
                      "report": "prepared.json" if args.prepare else "capture.json"}))


if __name__ == "__main__":
    main()

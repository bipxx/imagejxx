"""Observe eighteen fixed Composite Max cases in excluded ImageJ 1.54r tooling.

Prepare verifies the existing pinned source/runtime/classes, freezes inputs and
compiles a new observer, then probes hidden AWT wiring without dispatching any
R case. Capture is a separate root-supervised action. R01--R11 use explicitly
labelled public CompositeImage calls. R12--R18 bind a real hidden StackWindow and
ImageCanvas, then deliver an ItemEvent to the real Channels Choice; its unchanged
handler and bundled macro execute the transition.
No expected RGB, transition policy, product acceptance or OS-input claim is made.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import time
import traceback
import zipfile

sys.dont_write_bytecode = True
import capture_roi_boolean_reference as reference

ROOT = reference.ROOT
STAGE = ROOT / "build/references/composite-max-imagej154r-001"
CONTRACT = "p06c2.composite-max-reference.001"
CLASS_NAME = "CompositeMaxReference"
CASE_IDS = tuple(f"R{number:02d}" for number in range(1, 19))
STAGE_COUNTS = (3, 3, 3, 3, 7, 6, 3, 3, 3, 3, 6, 4, 4, 4, 4, 4, 4, 7)
RGB_COUNTS = (3, 3, 3, 3, 6, 5, 2, 2, 3, 2, 6, 3, 3, 3, 3, 3, 3, 6)
HOST_CASE_IDS = tuple(f"R{number:02d}" for number in range(12, 19))
HOST_SETUP_ORDER = (
    "construct_base_CompositeImage", "observe_before_attachment", "construct_original_ImageCanvas",
    "construct_hidden_StackWindow", "bind_current_window_and_temporary_image", "observe_after_attachment",
    "apply_fixture_channel_tables_and_ranges", "setPosition_through_StackWindow",
    "set_current_channel_LUT", "setDisplayRange", "set_projection_if_present", "setMode", "set_visibility",
    "set_canvas_source_rect", "set_canvas_drawing_size", "set_canvas_magnification_last", "observe_configured",
)
PAINT_METHOD = "ImageCanvas.paint(Graphics2D) on private BufferedImage.TYPE_INT_RGB"
RECEIPT = ROOT / "build/p06c-001/channel-display-entry-001.json"
RECEIPT_SHA = "5360efa8a9f605d845aec107d6d9e1ee940e9732d0bdafdbd07a4c213d1bb471"
REUSED_CLASSES = ROOT / "build/p06c-001/capture-001/classes"
MAX_PAYLOAD = 2 * 1024 * 1024
MAX_BUILD = 32 * 1024 * 1024
CAPTURE_SECONDS = 60
SOURCE_PINS = {
    "ij/IJ.java": "8c620842fedd63392268b4fee191e4f6e9ef749ce96bf14a3e9f936e7cea06b4",
    "ij/ImageJ.java": "e043a86f66cbb32c2ab2866502b0e37436b639171697ee706bcab92219d26a59",
    "ij/ImagePlus.java": "477e3f5f913b9c42feb68ea35d21836d45fd56920cccbfc5719a06822cbcfab9",
    "ij/WindowManager.java": "68dc7efb78b8c7fc446b5649a61ad8787366af6afc38f8fe62ae36ca5dddc2bf",
    "ij/gui/ImageCanvas.java": "ab27a57aa060ca8e297d59b09e478406ebd7a3bbd5d6a741bc1ab56c7cc553ab",
    "ij/gui/ImageWindow.java": "ca2cf35410411d8b53a02747ad2a5314598b9e79d201c9e632fef4b7aef5810b",
    "ij/gui/StackWindow.java": "333063c0a94206e656c1a67d72e7482397336644c911fc659f73539067146b57",
    "ij/CompositeImage.java": "7f97ef93b52967cfadda37fb8c26513de283787a93a918d3e54260e43e432a7f",
    "ij/plugin/frame/Channels.java": "172f378b8ece0839559f7c291dfcf98c6c9ec619f4055d3303282ba92ffd1ccc",
    "ij/plugin/Macro_Runner.java": "b9598bffa5b1bbbe851cbafdc31e43680176214020dbb53d6eea3d0665e9346b",
    "ij/process/ImageProcessor.java": "f98e87d25eb6b1f2c5d8557b73dd33e36fef0494d903d6d250206371dccc279c",
    "ij/process/ByteProcessor.java": "fc23c95778bd7154eba1ea56d9ea943e7d1d9ea568fe20cf9bf9c6db1db3ad94",
    "ij/process/ShortProcessor.java": "7484082e91c7095a3c79984c74b4dafa8b6d2912ed3b862e28dd331c8ba3024d",
    "ij/process/FloatProcessor.java": "0f52a157fe99e9a48ce2c9356fc2c3c435dc74f48e45a575171e60deb694ccf5",
    "ij/process/LUT.java": "c3e13b697521012d9bf132efd663c5b73350a09a2be07c95a48c4c105d5631ff",
    "ij/measure/Calibration.java": "2d44c799739c71b1fcdfcf6a5b42a86b65c46e4f6dca9b2cd29b6a479c67f07a",
    "macros/InvertAllLuts.txt": "266c77ef8ebae429260b4aca090e43854953630b637e870cb81a71521ad237cb",
}


def _table(name: str) -> str:
    """Generate only requested fixture bytes, never a rendering expectation."""
    rows = []
    colors = {"red": (1, 0, 0), "yellow": (1, 1, 0), "cyan": (0, 1, 1),
              "green": (0, 1, 0), "blue": (0, 0, 1), "white": (1, 1, 1),
              "magenta": (1, 0, 1)}
    if name.startswith("inverted_"):
        color = colors[name.removeprefix("inverted_")]
        rows = [tuple(255 - i * component for component in color) for i in range(256)]
    elif name in colors:
        rows = [tuple(i * component for component in colors[name]) for i in range(256)]
    elif name == "custom_a":
        rows = [((3 * i + 7) % 256, (5 * i + 11) % 256, (7 * i + 13) % 256) for i in range(256)]
    elif name == "custom_b":
        rows = [((11 * i + 23) % 256, (2 * i + 101) % 256, (13 * i + 3) % 256) for i in range(256)]
    elif name == "custom_c":
        rows = [((i * i + 41) % 256, (17 * i + 5) % 256, (255 - i) % 256) for i in range(256)]
    elif name == "constant":
        rows = [(37, 91, 143)] * 256
    elif name == "last_step_only":
        rows = [(200 - i // 2, 160 - i // 3, 120 - i // 4) for i in range(255)] + [(250, 240, 230)]
    else:
        raise ValueError("Unknown fixed input table " + name)
    return bytes(component for row in rows for component in row).hex()


def _fixture(name: str, dtype: str = "uint8", *, values=None, channels=3,
             width=5, height=1, times=1, slices=1, position=(0, 1, 0),
             ranges=None, tables=None, mode="composite", projection=None,
             flags=None, recipe=None, axes=None) -> dict:
    ranges = ranges or [[0, 255] for _ in range(channels)]
    tables = tables or ["red", "yellow", "cyan"]
    planes = []
    for t in range(times):
        for z in range(slices):
            for c in range(channels):
                samples = ([recipe(t, c, z, y, x) for y in range(height) for x in range(width)]
                           if recipe else values[c])
                if len(samples) != width * height:
                    raise ValueError("Fixed source plane shape differs")
                if dtype == "float32":
                    # An integer is explicitly an IEEE binary32 input bit word.
                    data = b"".join(struct.pack("<I", sample) if type(sample) is int
                                    else struct.pack("<f", sample) for sample in samples)
                else:
                    data = b"".join(struct.pack("<B" if dtype == "uint8" else "<H", sample) for sample in samples)
                planes.append({"t": t, "c": c, "z": z, "bits_hex": data.hex()})
    if len(ranges) != channels or len(tables) != channels:
        raise ValueError("Fixed per-channel input length differs")
    return {"name": name, "dtype": dtype, "shape_tczyx": [times, channels, slices, height, width],
            "position_tcz": list(position), "mode": mode, "projection": projection,
            "visibility": flags if flags is not None else [True] * channels,
            "channels": [{"requested_range": interval, "table_name": table,
                          "rgb_interleaved_hex": _table(table) if table != "reference_default" else None}
                         for interval, table in zip(ranges, tables)],
            "planes": planes, "bits_encoding": "little_endian_scalar_bits",
            "active_processor_setup": "retain_reference_gray_processor" if channels == 1 else "setChannelLut(current_selected_channel)",
            "construction_axes": axes or {"rows": "Y", "columns": "X"},
            "calibration": {"spacing_xyz": [.5, 1.25, 2.5], "origin_xyz": [11, -7, 19],
                            "unit": "um", "frame_interval": .75, "time_unit": "s"}}


def _action(image: str, label: str, operation="observe", *, render=True, **inputs) -> dict:
    return {"image": image, "label": label, "operation": operation, "render": render, **inputs}


def cases() -> list[dict]:
    """The complete fixed input/stage membership, with no output calculations."""
    rows = []
    low_mid_high = [[0, 31, 127, 192, 255], [255, 64, 127, 128, 0], [13, 250, 65, 128, 201]]

    def add(number, fixtures, actions, question):
        rows.append({"id": f"R{number:02d}", "driving_method": "actual_Channels_Choice_ItemEvent" if number >= 12
                     else "programmatic_CompositeImage_public_API", "question": question,
                     "fixtures": fixtures, "actions": actions})

    basic = _fixture("main", values=low_mid_high)
    add(1, [basic], [_action("main", "sum"), _action("main", "max", "projection", value="Max"),
                    _action("main", "max.redraw")], "Overlapping components; low/middle/high mapped values")
    add(2, [_fixture("main", "uint16", values=[[0, 1, 5, 10, 11], [2, 6, 8, 11, 15], [0, 7, 100, 255, 511]],
                     ranges=[[.2, 10.8], [.6, 10.4], [7.25, 255.75]])],
        [_action("main", "sum"), _action("main", "max", "projection", value="Max"), _action("main", "max.redraw")],
        "Distinct fractional requested UInt16 endpoints")
    endpoint_bits = [0x00000000, 0x3DCCCCCC, 0x3DCCCCCD, 0x3DCCCCCE, 0x3E4CCCCD, 0x3E99999A,
                     0x3F000000, 0x3F333333, 0x3F666665, 0x3F666666, 0x3F666667, 0x3F800000]
    add(3, [_fixture("main", "float32", values=[endpoint_bits, endpoint_bits[::-1], endpoint_bits[4:] + endpoint_bits[:4]],
                     width=12, ranges=[[.1, .9]] * 3)],
        [_action("main", "sum"), _action("main", "max", "projection", value="Max"), _action("main", "max.redraw")],
        "Pinned 0.1/0.9 endpoint bit discriminators before Max")
    add(4, [_fixture("main", values=low_mid_high, tables=["custom_a", "custom_b", "custom_c"])],
        [_action("main", "sum"), _action("main", "max", "projection", value="Max"), _action("main", "max.redraw")],
        "Nonlinear tables, nonblack entry zero, independent RGB components")
    hidden = [False, True, True]
    add(5, [_fixture("cold", values=low_mid_high, projection="Max", flags=hidden),
            _fixture("warm", values=low_mid_high)],
        [_action("cold", "cold.before", render=False), _action("cold", "cold.max"), _action("cold", "cold.max.redraw"),
         _action("warm", "warm.sum"), _action("warm", "warm.hidden.sum", "visibility", value=hidden),
         _action("warm", "warm.max", "projection", value="Max"), _action("warm", "warm.max.redraw")],
        "First hidden channel, cold versus populated SUM cache")
    add(6, [_fixture("cold", values=low_mid_high, projection="Max", flags=[False] * 3),
            _fixture("warm", values=low_mid_high, projection="Max")],
        [_action("cold", "cold.before", render=False), _action("cold", "cold.empty"), _action("cold", "cold.empty.redraw"),
         _action("warm", "warm.populated"), _action("warm", "warm.empty", "visibility", value=[False] * 3),
         _action("warm", "warm.empty.redraw")], "All hidden cold and after populated Max")
    add(7, [_fixture("main", values=[low_mid_high[0]], channels=1, tables=["custom_a"], position=(0, 0, 0), projection="Max")],
        [_action("main", "singleton.before", render=False), _action("main", "singleton.max"), _action("main", "singleton.redraw")],
        "Singleton Composite custom table and gray fast path")
    add(8, [_fixture("main", channels=8, tables=["reference_default"] * 8, position=(0, 6, 0), projection="Max",
                     flags=[True, False, True, True, False, True, True, True],
                     values=[[(c * 29 + x * 47 + 11) % 256 for x in range(5)] for c in range(8)])],
        [_action("main", "eight.before", render=False), _action("main", "eight.max"), _action("main", "eight.redraw")],
        "All eight default tables including White and nontrivial visibility")
    add(9, [_fixture("main", "uint16", channels=9, tables=["reference_default"] * 9,
                     ranges=[[0, 511]] * 9, position=(0, 8, 0),
                     values=[[(c * 31 + x * 97) % 512 for x in range(5)] for c in range(9)])],
        [_action("main", "nine.color"), _action("main", "nine.max_request", "projection_mode", value="Max", mode="composite"),
         _action("main", "nine.redraw")], "C9 explicit Composite Max request, Color fallback and active C9")
    quiet = [0x7FC12345, 0x80000000, 0x00000000, 0xBF800000, 0x3F000000, 0x40000000]
    add(10, [_fixture("main", "float32", width=6, values=[quiet, quiet[::-1], quiet[2:] + quiet[:2]],
                      ranges=[[0, 1]] * 3, tables=["custom_a", "custom_b", "custom_c"], projection="Max")],
        [_action("main", "quiet.before", render=False), _action("main", "quiet.max"), _action("main", "quiet.redraw")],
        "Quiet NaN payload, signed zero, finite negative/outside range and custom entry zero")
    scalar = lambda t, c, z, y, x: 40 * t + 30 * c + 5 * z + 2 * y + x
    add(11, [_fixture("xy", "uint16", width=5, height=3, times=2, slices=4, position=(1, 1, 2),
                      recipe=scalar, projection="Max"),
             _fixture("xz", "uint16", width=5, height=4, position=(0, 1, 0), projection="Max",
                      recipe=lambda t, c, z, row, x: scalar(1, c, row, 1, x),
                      axes={"rows": "Z", "columns": "X", "fixed_t": 1, "fixed_y": 1}),
             _fixture("yz", "uint16", width=3, height=4, position=(0, 1, 0), projection="Max",
                      recipe=lambda t, c, z, row, y: scalar(1, c, row, y, 1),
                      axes={"rows": "Z", "columns": "Y", "fixed_t": 1, "fixed_x": 1})],
        [_action(name, name + suffix) for name in ("xy", "xz", "yz") for suffix in (".max", ".redraw")],
        "Asymmetric TCZ scalar XY and separately constructed XZ/YZ compositor inputs; not orthogonal UI")
    transition_values = lambda t, c, z, y, x: (40 * t + 30 * c + 5 * z + 2 * y + x) % 256
    for number in range(12, 19):
        options = dict(width=5, height=3, times=2, slices=4, position=(1, 1, 2), recipe=transition_values,
                       ranges=[[1, 201], [7, 219], [11, 241]], flags=[True, False, True])
        options["tables"] = ["red", "yellow", "cyan"]
        if number == 13:
            options.update(mode="color", projection="Max", flags=[False, True, False])
        elif number == 14:
            options.update(mode="grayscale", projection="Max", tables=["inverted_red", "inverted_yellow", "inverted_cyan"])
        elif number == 15:
            options.update(position=(1, 2, 2), tables=["inverted_red", "inverted_yellow", "inverted_cyan"])
        elif number == 16:
            options.update(tables=["red", "inverted_yellow", "custom_c"])
        elif number == 17:
            options.update(tables=["red", "constant", "custom_b"])
        elif number == 18:
            options.update(tables=["red", "last_step_only", "custom_a"])
        # Setup warming is explicitly a programmatic input. Reapply requested
        # flags after it, so R13 starts with deliberately differing flags.
        actions = [_action("main", "setup.warmed"),
                   _action("main", "before", "visibility", value=options["flags"], render=False),
                   _action("main", "choice.max", "channels_choice", index=4, render=False),
                   _action("main", "after.redraw")]
        if number == 18:
            actions.extend([_action("main", "choice.max.repeated", "channels_choice", index=4, render=False),
                            _action("main", "after.repeated.redraw"), _action("main", "after.repeated.redraw2")])
        add(number, [_fixture("main", **options)], actions,
            {12: "Composite SUM to Max with hidden flags and non-inverting displayed LUT",
             13: "Color to Max with retained Max property and differing flags",
             14: "Gray displayed processor versus inverted stored channel tables",
             15: "Uniformly inverted tables and active C3",
             16: "Active C2 inverted, C1 ascending and mixed custom table",
             17: "Constant active custom table classifier and all-channel transition",
             18: "Sole ascending step 254 to 255 and repeated real Max selection"}[number])
    if tuple(row["id"] for row in rows) != CASE_IDS:
        raise ValueError("Fixed eighteen-case membership differs")
    if (tuple(len(row["actions"]) for row in rows) != STAGE_COUNTS
            or tuple(sum(action["render"] or action["operation"] == "channels_choice" for action in row["actions"]) for row in rows) != RGB_COUNTS
            or sum(len(row["fixtures"]) for row in rows) != 22
            or sum(action["operation"] == "channels_choice" for row in rows for action in row["actions"]) != 8):
        raise ValueError("Fixed 74-stage/62-RGB/22-source-pair/8-dispatch membership differs")
    return rows


DRIVER = r'''import ij.CompositeImage;
import ij.IJ;
import ij.ImageJ;
import ij.ImagePlus;
import ij.ImageStack;
import ij.Prefs;
import ij.WindowManager;
import ij.gui.ImageCanvas;
import ij.gui.ImageWindow;
import ij.gui.ScrollbarWithLabel;
import ij.gui.StackWindow;
import ij.measure.Calibration;
import ij.plugin.frame.Channels;
import ij.process.*;
import java.awt.Choice;
import java.awt.EventQueue;
import java.awt.Frame;
import java.awt.GraphicsEnvironment;
import java.awt.Graphics2D;
import java.awt.Image;
import java.awt.Rectangle;
import java.awt.Window;
import java.awt.event.ItemEvent;
import java.awt.event.ItemListener;
import java.awt.image.BufferedImage;
import java.awt.image.PixelGrabber;
import java.io.InputStream;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

public final class CompositeMaxReference {
    static final String CONTRACT="p06c2.composite-max-reference.001";
    static final List<Object> CASES=__CASES__;
    static ImageJ app;
    static HiddenChannels channels;
    static Choice choice;
    static int itemEvents;
    static final Map<CompositeImage,Host> HOSTS=new IdentityHashMap<>();
    static final List<Object> HOST_ORDER=__HOST_ORDER__;
    static final class HiddenStackWindow extends StackWindow {
        HiddenStackWindow(ImagePlus image,ImageCanvas canvas){super(image,canvas);}
        @Override public void show() {} // Presentation only; position/render methods are inherited.
        @Override public void setVisible(boolean visible){super.setVisible(false);}
    }
    static final class Host {
        final CompositeImage image;final ImageCanvas canvas;final HiddenStackWindow window;final BufferedImage surface;
        int paints;String lastPaintLabel;
        Host(CompositeImage image){this.image=image;canvas=new ImageCanvas(image);window=new HiddenStackWindow(image,canvas);
            surface=new BufferedImage(image.getWidth()*8,image.getHeight()*8,BufferedImage.TYPE_INT_RGB);}
    }
    static final class HiddenChannels extends Channels {
        @Override public void show() {} // Presentation only; dispatch is inherited unchanged.
        @Override public void setVisible(boolean visible) {super.setVisible(false);}
    }
    static Map<String,Object> o(Object...pairs){Map<String,Object> out=new LinkedHashMap<>();
        for(int i=0;i<pairs.length;i+=2)out.put((String)pairs[i],pairs[i+1]);return out;}
    static List<Object> a(Object...items){return new ArrayList<>(Arrays.asList(items));}
    @SuppressWarnings("unchecked") static Map<String,Object> m(Object value){return (Map<String,Object>)value;}
    @SuppressWarnings("unchecked") static List<Object> l(Object value){return (List<Object>)value;}
    static int n(Object value){return ((Number)value).intValue();}
    static double d(Object value){return ((Number)value).doubleValue();}
    static String quote(String value){StringBuilder b=new StringBuilder("\"");for(int i=0;i<value.length();i++){
        char c=value.charAt(i);if(c=='"'||c=='\\')b.append('\\').append(c);else if(c<32||c>127)b.append(String.format("\\u%04x",(int)c));else b.append(c);}return b.append('"').toString();}
    static String json(Object value){if(value==null)return "null";if(value instanceof String)return quote((String)value);
        if(value instanceof Number||value instanceof Boolean)return value.toString();StringBuilder b=new StringBuilder();
        if(value instanceof Map){b.append('{');boolean first=true;for(Object raw:((Map<?,?>)value).entrySet()){
            Map.Entry<?,?> entry=(Map.Entry<?,?>)raw;if(!first)b.append(',');first=false;b.append(quote((String)entry.getKey())).append(':').append(json(entry.getValue()));}return b.append('}').toString();}
        b.append('[');boolean first=true;if(value instanceof Iterable){for(Object item:(Iterable<?>)value){if(!first)b.append(',');first=false;b.append(json(item));}}
        else if(value.getClass().isArray()){for(int i=0;i<Array.getLength(value);i++){if(i>0)b.append(',');b.append(json(Array.get(value,i)));}}
        else throw new IllegalArgumentException("Unsupported observation "+value.getClass());return b.append(']').toString();}
    static byte[] bytes(String hex){byte[] out=new byte[hex.length()/2];for(int i=0;i<out.length;i++)out[i]=(byte)Integer.parseInt(hex.substring(i*2,i*2+2),16);return out;}
    static String hex(byte[] input){StringBuilder b=new StringBuilder(input.length*2);for(byte value:input)b.append(String.format("%02x",value&255));return b.toString();}
    static String sha(byte[] input)throws Exception{return hex(MessageDigest.getInstance("SHA-256").digest(input));}
    static int word(byte[] input,int offset,int count){int value=0;for(int i=0;i<count;i++)value|=(input[offset+i]&255)<<(i*8);return value;}
    static ImageProcessor processor(String type,int w,int h,String raw){byte[] input=bytes(raw);int size=w*h;
        if(type.equals("uint8")){if(input.length!=size)throw new IllegalArgumentException("U8 input shape");return new ByteProcessor(w,h,input,null);}
        if(type.equals("uint16")){if(input.length!=size*2)throw new IllegalArgumentException("U16 input shape");short[] out=new short[size];for(int i=0;i<size;i++)out[i]=(short)word(input,i*2,2);return new ShortProcessor(w,h,out,null);}
        if(input.length!=size*4)throw new IllegalArgumentException("F32 input shape");float[] out=new float[size];for(int i=0;i<size;i++)out[i]=Float.intBitsToFloat(word(input,i*4,4));return new FloatProcessor(w,h,out,null);}
    static LUT lut(String raw,double low,double high){byte[] input=bytes(raw),r=new byte[256],g=new byte[256],b=new byte[256];
        if(input.length!=768)throw new IllegalArgumentException("Table length");for(int i=0;i<256;i++){r[i]=input[i*3];g[i]=input[i*3+1];b[i]=input[i*3+2];}
        LUT result=new LUT(r,g,b);result.min=low;result.max=high;return result;}
    static String table(java.awt.image.IndexColorModel lut){byte[] out=new byte[768];for(int i=0;i<256;i++){out[i*3]=(byte)lut.getRed(i);out[i*3+1]=(byte)lut.getGreen(i);out[i*3+2]=(byte)lut.getBlue(i);}return hex(out);}
    static int mode(String name){return name.equals("composite")?CompositeImage.COMPOSITE:name.equals("color")?CompositeImage.COLOR:CompositeImage.GRAYSCALE;}
    static void visibility(CompositeImage image,List<Object> values){boolean[] active=image.getActiveChannels();
        for(int i=0;i<Math.min(values.size(),active.length);i++)active[i]=(Boolean)values.get(i);}
    static CompositeImage build(Map<String,Object> spec,boolean hosted,Map<String,Object> setups){List<Object> shape=l(spec.get("shape_tczyx"));
        int t=n(shape.get(0)),c=n(shape.get(1)),z=n(shape.get(2)),h=n(shape.get(3)),w=n(shape.get(4));
        ImageStack stack=new ImageStack(w,h);for(Object raw:l(spec.get("planes"))){Map<String,Object> plane=m(raw);
            stack.addSlice(processor((String)spec.get("dtype"),w,h,(String)plane.get("bits_hex")));}
        ImagePlus source=new ImagePlus((String)spec.get("name"),stack);source.setDimensions(c,z,t);
        Map<String,Object> requested=m(spec.get("calibration"));Calibration cal=new Calibration();
        List<Object> spacing=l(requested.get("spacing_xyz")),origin=l(requested.get("origin_xyz"));
        cal.pixelWidth=d(spacing.get(0));cal.pixelHeight=d(spacing.get(1));cal.pixelDepth=d(spacing.get(2));
        cal.xOrigin=d(origin.get(0));cal.yOrigin=d(origin.get(1));cal.zOrigin=d(origin.get(2));cal.setUnit((String)requested.get("unit"));
        cal.frameInterval=d(requested.get("frame_interval"));cal.setTimeUnit((String)requested.get("time_unit"));source.setCalibration(cal);
        CompositeImage image=new CompositeImage(source,CompositeImage.COMPOSITE);
        Map<String,Object> setup=null;
        if(hosted){Object before=state(image);Host host=new Host(image);HOSTS.put(image,host);bind(image);hidden();
            if(image.getNFrames()!=t||image.getNChannels()!=c||image.getNSlices()!=z||image.getWidth()!=w||image.getHeight()!=h)
                throw new IllegalStateException("StackWindow construction changed fixture dimensions");
            setup=o("order",HOST_ORDER,"before_attachment",before,"after_attachment",state(image));setups.put((String)spec.get("name"),setup);}
        List<Object> requestedChannels=l(spec.get("channels"));
        for(int i=0;i<c;i++){Map<String,Object> channel=m(requestedChannels.get(i));List<Object> range=l(channel.get("requested_range"));
            String raw=(String)channel.get("rgb_interleaved_hex");LUT table=raw==null?(LUT)image.getChannelLut(i+1).clone():lut(raw,d(range.get(0)),d(range.get(1)));
            table.min=d(range.get(0));table.max=d(range.get(1));image.setChannelLut(table,i+1);}
        // The ordinary setter renders on a channel change even without a
        // window. Preserve genuine cold cases with the public no-update API.
        List<Object> position=l(spec.get("position_tcz"));
        if(hosted)image.setPosition(n(position.get(1))+1,n(position.get(2))+1,n(position.get(0))+1);
        else image.setPositionWithoutUpdate(n(position.get(1))+1,n(position.get(2))+1,n(position.get(0))+1);
        // A windowless TCZ setter can leave ip holding the former channel's
        // table. Set the requested active table explicitly with the public
        // current-channel API, so inversion tests really concern selected C.
        // Singleton intentionally retains its reference gray processor while
        // the separately stored channel table is non-gray.
        if(c>1)image.setChannelLut((LUT)image.getChannelLut(n(position.get(1))+1).clone());
        List<Object> activeRange=l(m(requestedChannels.get(n(position.get(1)))).get("requested_range"));image.setDisplayRange(d(activeRange.get(0)),d(activeRange.get(1)));
        if(spec.get("projection")!=null)image.setProp("CompositeProjection",(String)spec.get("projection"));
        image.setMode(mode((String)spec.get("mode")));visibility(image,l(spec.get("visibility")));
        if(hosted){Host host=HOSTS.get(image);host.canvas.setSourceRect(new Rectangle(0,0,w,h));host.canvas.setDrawingSize(w*8,h*8);host.canvas.setMagnification(8);
            bind(image);setup.put("configured",state(image));}return image;}
    static String scalarBits(Object pixels){int count=Array.getLength(pixels),bytes=pixels instanceof byte[]?1:pixels instanceof short[]?2:4;byte[] out=new byte[count*bytes];
        for(int i=0;i<count;i++){int word=pixels instanceof byte[]?((byte[])pixels)[i]&255:pixels instanceof short[]?((short[])pixels)[i]&65535:Float.floatToRawIntBits(((float[])pixels)[i]);
            for(int j=0;j<bytes;j++)out[i*bytes+j]=(byte)(word>>>(j*8));}return hex(out);}
    static Object source(CompositeImage image){List<Object> planes=new ArrayList<>();
        for(int t=0;t<image.getNFrames();t++)for(int z=0;z<image.getNSlices();z++)for(int c=0;c<image.getNChannels();c++){
            int index=image.getStackIndex(c+1,z+1,t+1);planes.add(o("t",t,"c",c,"z",z,"stack_index",index,"bits_hex",scalarBits(image.getStack().getPixels(index))));}
        return o("shape_tczyx",a(image.getNFrames(),image.getNChannels(),image.getNSlices(),image.getHeight(),image.getWidth()),
            "bits_encoding","little_endian_scalar_bits","bit_depth",image.getBitDepth(),"planes",planes);}
    static Object field(Class<?> owner,Object target,String name){try{Field field=owner.getDeclaredField(name);field.setAccessible(true);return field.get(target);}
        catch(Exception error){throw new IllegalStateException("Cannot observe reference cache field "+name,error);}}
    static void bind(CompositeImage image){Host host=HOSTS.get(image);if(host!=null)WindowManager.setCurrentWindow(host.window);WindowManager.setTempCurrentImage(image);}
    static Object selector(HiddenStackWindow window,String name){ScrollbarWithLabel actual=(ScrollbarWithLabel)field(StackWindow.class,window,name);return actual==null?null:actual.getValue()-1;}
    static Object hostState(CompositeImage image){Host host=HOSTS.get(image);if(host==null)return null;
        if(image.getWindow()!=host.window||host.window.getImagePlus()!=image||image.getCanvas()!=host.canvas||host.window.getCanvas()!=host.canvas||host.canvas.getImage()!=image)
            throw new IllegalStateException("Actual image/StackWindow/original ImageCanvas binding changed");
        if(host.window.isVisible()||host.window.isShowing()||!image.getOpenAsHyperStack()||!image.isDisplayedHyperStack()||!host.window.validDimensions())
            throw new IllegalStateException("Actual hidden displayed-hyperstack host admission changed");
        ImageProcessor processor=image.getProcessor();int index=image.getCurrentSlice();Object pixels=processor.getPixels(),source=image.getStack().getPixels(index);Rectangle rect=host.canvas.getSrcRect();
        return o("image_id",image.getID(),"image_identity",System.identityHashCode(image),"window_identity",System.identityHashCode(host.window),"canvas_identity",System.identityHashCode(host.canvas),
            "window_class",host.window.getClass().getName(),"canvas_class",host.canvas.getClass().getName(),
            "image_window_identity",System.identityHashCode(image.getWindow()),"window_image_id",host.window.getImagePlus().getID(),"window_image_identity",System.identityHashCode(host.window.getImagePlus()),
            "window_canvas_identity",System.identityHashCode(host.window.getCanvas()),"canvas_image_id",host.canvas.getImage().getID(),"canvas_image_identity",System.identityHashCode(host.canvas.getImage()),
            "canvas_parent_identity",System.identityHashCode(host.canvas.getParent()),"current_window_is_host",WindowManager.getCurrentWindow()==host.window,"current_image_is_source",WindowManager.getCurrentImage()==image,
            "open_as_hyperstack",image.getOpenAsHyperStack(),"displayed_hyperstack",image.isDisplayedHyperStack(),"window_hyperstack",host.window.isHyperStack(),"valid_dimensions",host.window.validDimensions(),
            "shape_tczyx",a(image.getNFrames(),image.getNChannels(),image.getNSlices(),image.getHeight(),image.getWidth()),"position_tcz",a(image.getFrame()-1,image.getChannel()-1,image.getSlice()-1),
            "current_slice_index",index-1,"stack_size",image.getStackSize(),
            "window_position_czt",a(n(field(StackWindow.class,host.window,"c"))-1,n(field(StackWindow.class,host.window,"z"))-1,n(field(StackWindow.class,host.window,"t"))-1),
            "selector_position_czt",a(selector(host.window,"cSelector"),selector(host.window,"zSelector"),selector(host.window,"tSelector")),
            "window",o("visible",host.window.isVisible(),"showing",host.window.isShowing(),"displayable",host.window.isDisplayable()),
            "canvas",o("visible",host.canvas.isVisible(),"showing",host.canvas.isShowing(),"displayable",host.canvas.isDisplayable(),"magnification",host.canvas.getMagnification(),
                "source_rect_xywh",a(rect.x,rect.y,rect.width,rect.height),"drawing_wh",a(host.canvas.getWidth(),host.canvas.getHeight()),
                "image_updated",field(ImageCanvas.class,host.canvas,"imageUpdated"),"paint_pending",host.canvas.getPaintPending()),
            "processor_binding",o("identity",System.identityHashCode(processor),"pixels_identity",System.identityHashCode(pixels),"source_pixels_identity",System.identityHashCode(source),
                "source_stack_index",index-1,"pixels_are_source",pixels==source,"scalar_bits_hex",scalarBits(pixels),"source_scalar_bits_hex",scalarBits(source)),
            "offscreen_paint",o("method","ImageCanvas.paint(Graphics2D) on private BufferedImage.TYPE_INT_RGB","count",host.paints,"last_label",host.lastPaintLabel,
                "surface_wh",a(host.surface.getWidth(),host.surface.getHeight())));}
    static void paint(CompositeImage image,String label){Host host=HOSTS.get(image);if(host==null)return;Graphics2D graphics=host.surface.createGraphics();
        try{host.canvas.paint(graphics);host.paints++;host.lastPaintLabel=label;}finally{graphics.dispose();}}
    static void closeHost(CompositeImage image){Host host=HOSTS.remove(image);if(host==null)return;image.changes=false;
        if(!host.window.close())throw new IllegalStateException("Hidden reference host cleanup was canceled");}
    static Object state(CompositeImage image){List<Object> tables=new ArrayList<>();LUT[] luts=image.getLuts();
        for(int c=0;c<luts.length;c++)tables.add(o("channel",c,"effective_range",a(luts[c].min,luts[c].max),"rgb_interleaved_hex",table(luts[c])));
        ImageProcessor active=image.getProcessor();Calibration cal=image.getCalibration();
        return o("image_id",image.getID(),"mode",image.getModeAsString(),"mode_integer",image.getMode(),"projection",image.getProp("CompositeProjection"),
            "host",hostState(image),
            "position_tcz",a(image.getFrame()-1,image.getChannel()-1,image.getSlice()-1),"channel_count",image.getNChannels(),
            "active_flags",image.getActiveChannels().clone(),"channel_state",tables,
            "cache",o("rgb_pixels_present",field(CompositeImage.class,image,"rgbPixels")!=null,
                "channel_processors_present",field(CompositeImage.class,image,"cip")!=null,
                "image_present",field(ImagePlus.class,image,"img")!=null,
                "single_channel",field(CompositeImage.class,image,"singleChannel"),
                "current_channel",field(CompositeImage.class,image,"currentChannel"),
                "current_slice",field(CompositeImage.class,image,"currentSlice"),"current_frame",field(CompositeImage.class,image,"currentFrame")),
            "active_processor",o("class",active.getClass().getName(),"range",a(active.getMin(),active.getMax()),
                "is_inverted_lut",active.isInvertedLut(),"rgb_interleaved_hex",table((java.awt.image.IndexColorModel)active.getColorModel())),
            "calibration",o("spacing_xyz",a(cal.pixelWidth,cal.pixelHeight,cal.pixelDepth),"origin_xyz",a(cal.xOrigin,cal.yOrigin,cal.zOrigin),
                "unit",cal.getUnit(),"frame_interval",cal.frameInterval,"time_unit",cal.getTimeUnit()));}
    static Object rgb(CompositeImage image)throws Exception{Image rendered=image.getImage();int w=image.getWidth(),h=image.getHeight();int[] pixels=new int[w*h];
        String method;if(rendered instanceof BufferedImage){((BufferedImage)rendered).getRGB(0,0,w,h,pixels,0,w);method="BufferedImage.getRGB";}
        else{PixelGrabber grabber=new PixelGrabber(rendered,0,0,w,h,pixels,0,w);if(!grabber.grabPixels(1000))throw new IllegalStateException("RGB readback exceeded 1000 ms");method="PixelGrabber";}
        byte[] packed=new byte[pixels.length*3];for(int i=0;i<pixels.length;i++){packed[i*3]=(byte)(pixels[i]>>>16);packed[i*3+1]=(byte)(pixels[i]>>>8);packed[i*3+2]=(byte)pixels[i];}
        return o("shape_yx",a(h,w),"rgb_interleaved_hex",hex(packed),"method",method);}
    static void hidden(){for(Window window:Window.getWindows())if(window.isVisible()||window.isShowing())throw new IllegalStateException("A reference OS window became visible: "+window.getClass());}
    static void ensureChannels()throws Exception{if(channels!=null){channels.update();return;}channels=new HiddenChannels();
        Field field=Channels.class.getDeclaredField("choice");field.setAccessible(true);choice=(Choice)field.get(channels);
        choice.addItemListener(event->{itemEvents++;});hidden();}
    static Object choiceState(){List<Object> items=new ArrayList<>(),listeners=new ArrayList<>();for(int i=0;i<choice.getItemCount();i++)items.add(choice.getItem(i));
        boolean registered=false;for(ItemListener listener:choice.getItemListeners()){listeners.add(listener.getClass().getName());if(listener==channels)registered=true;}
        String declaring;try{declaring=channels.getClass().getMethod("itemStateChanged",ItemEvent.class).getDeclaringClass().getName();}
        catch(Exception error){throw new IllegalStateException("Cannot observe actual Channels handler",error);}
        return o("items",items,"selected_index",choice.getSelectedIndex(),"selected_item",choice.getSelectedItem(),"identity",System.identityHashCode(choice),
            "listeners",listeners,"channels_listener_registered",registered,"handler_declaring_class",declaring,
            "visible",channels.isVisible(),"showing",channels.isShowing(),"delivered_item_events",itemEvents);}
    static Object dispatch(CompositeImage image,int index)throws Exception{bind(image);ensureChannels();
        Object before=choiceState();int count=itemEvents;choice.select(index);ItemEvent event=new ItemEvent(choice,ItemEvent.ITEM_STATE_CHANGED,choice.getItem(index),ItemEvent.SELECTED);
        choice.dispatchEvent(event);if(itemEvents!=count+1)throw new IllegalStateException("Actual Choice did not deliver exactly one ItemEvent");
        if(WindowManager.getCurrentImage()!=image)throw new IllegalStateException("Channels lost the explicitly bound current image");hidden();
        return o("method","Choice.dispatchEvent(ItemEvent)","event_id",event.getID(),"state_change",event.getStateChange(),"event_item",event.getItem(),
            "source_identity",System.identityHashCode(event.getSource()),"requested_index",index,"before",before,"after",choiceState(),
            "current_image_id",WindowManager.getCurrentImage().getID(),"state_after_handler_before_readback",state(image));}
    static Object environment()throws Exception{InputStream stream=ImageJ.class.getResourceAsStream("/macros/InvertAllLuts.txt");
        if(stream==null)throw new IllegalStateException("Real Channels macro resource is unavailable");byte[] macro=stream.readAllBytes();stream.close();
        List<Object> windows=new ArrayList<>();for(Window window:Window.getWindows())windows.add(o("class",window.getClass().getName(),"visible",window.isVisible(),"showing",window.isShowing(),"displayable",window.isDisplayable()));
        return o("imagej_version",IJ.getVersion(),"java_version",System.getProperty("java.version"),"java_vendor",System.getProperty("java.vendor"),
            "os_name",System.getProperty("os.name"),"awt_headless",GraphicsEnvironment.isHeadless(),"event_dispatch_thread",EventQueue.isDispatchThread(),
            "gui_scale",Prefs.getGuiScale(),"auto_contrast",Prefs.autoContrast,"use_inverting_lut",Prefs.useInvertingLut,
            "imagej_visible",app.isVisible(),"imagej_showing",app.isShowing(),"macro_resource",o("name","/macros/InvertAllLuts.txt","bytes",macro.length,"sha256",sha(macro)),
            "windows",windows,"loaded_reference_classes",o("Channels",Channels.class.getProtectionDomain().getCodeSource().getLocation().toString(),
                "CompositeImage",CompositeImage.class.getProtectionDomain().getCodeSource().getLocation().toString(),
                "ImageProcessor",ImageProcessor.class.getProtectionDomain().getCodeSource().getLocation().toString(),
                "ImageCanvas",ImageCanvas.class.getProtectionDomain().getCodeSource().getLocation().toString(),
                "ImageWindow",ImageWindow.class.getProtectionDomain().getCodeSource().getLocation().toString(),
                "StackWindow",StackWindow.class.getProtectionDomain().getCodeSource().getLocation().toString()),
            "scope","programmatic CompositeImage R01-R11; hidden StackWindow/ImageCanvas and real Channels Choice R12-R18; offscreen paint, no OS input");}
    static Object capture(Map<String,Object> spec)throws Exception{long started=System.nanoTime();Map<String,CompositeImage> images=new LinkedHashMap<>();
        Map<String,Object> beforeSources=new LinkedHashMap<>(),afterSources=new LinkedHashMap<>(),setups=new LinkedHashMap<>();
        boolean hosted=((String)spec.get("id")).compareTo("R12")>=0;
        for(Object raw:l(spec.get("fixtures"))){Map<String,Object> fixture=m(raw);CompositeImage image=build(fixture,hosted,setups);String name=(String)fixture.get("name");images.put(name,image);beforeSources.put(name,source(image));}
        List<Object> stages=new ArrayList<>();for(Object raw:l(spec.get("actions"))){Map<String,Object> action=m(raw);CompositeImage image=images.get((String)action.get("image"));
            bind(image);Object before=state(image),delivered=null;String operation=(String)action.get("operation");long stageStarted=System.nanoTime();
            if(operation.equals("projection")||operation.equals("projection_mode"))image.setProp("CompositeProjection",(String)action.get("value"));
            if(operation.equals("projection_mode"))image.setMode(mode((String)action.get("mode")));
            if(operation.equals("visibility"))visibility(image,l(action.get("value")));
            if(operation.equals("channels_choice"))delivered=dispatch(image,n(action.get("index")));
            if((Boolean)action.get("render"))image.updateImage();
            // Hosted readbacks paint the original real canvas offscreen after
            // the operation/handler and before reading the same image's RGB.
            // Programmatic cold/warm paths have no host and are unchanged.
            boolean read=(Boolean)action.get("render")||operation.equals("channels_choice");if(read)paint(image,(String)action.get("label"));
            Object pixels=read?rgb(image):null;
            hidden();stages.add(o("label",action.get("label"),"input",action,"before",before,"after",state(image),"rgb",pixels,"dispatch",delivered,
                "elapsed_ms",(System.nanoTime()-stageStarted)/1000000.0));}
        for(Map.Entry<String,CompositeImage> entry:images.entrySet())afterSources.put(entry.getKey(),source(entry.getValue()));
        Object env=environment();for(CompositeImage image:images.values())closeHost(image);
        WindowManager.setTempCurrentImage(null);hidden();return o("contract",CONTRACT,"id",spec.get("id"),"input_contract",spec,
            "host_setup",setups,"source_before",beforeSources,"stages",stages,"source_after",afterSources,"environment",env,"elapsed_ms",(System.nanoTime()-started)/1000000.0);}
    static <T>T edt(Callable<T> work,int seconds)throws Exception{FutureTask<T> future=new FutureTask<>(work);EventQueue.invokeLater(future);return future.get(seconds,TimeUnit.SECONDS);}
    public static void main(String[]args){try{edt(()->{app=new ImageJ(ImageJ.NO_SHOW);Prefs.setGuiScale(1);Prefs.autoContrast=false;Prefs.useInvertingLut=false;hidden();return null;},10);
        if(args.length!=1)throw new IllegalArgumentException("Expected --probe or --capture");
        if(args[0].equals("--probe")){System.out.println(json(edt(()->{Map<String,Object> setups=new LinkedHashMap<>();CompositeImage image=build(m(l(m(CASES.get(11)).get("fixtures")).get(0)),true,setups);
            bind(image);ensureChannels();paint(image,"probe");Object observed=o("contract",CONTRACT,"kind","environment_probe_no_R_dispatch", "environment",environment(),"choice",choiceState(),"host_setup",setups,"state",state(image));
            closeHost(image);WindowManager.setTempCurrentImage(null);return observed;},10)));}
        else if(args[0].equals("--capture")){long start=System.nanoTime();for(Object raw:CASES){if((System.nanoTime()-start)/1e9>45)throw new IllegalStateException("Reference Java parent exceeded 45 seconds");
            System.out.println(json(edt(()->capture(m(raw)),5)));System.out.flush();}}
        else throw new IllegalArgumentException("Unknown action");
        edt(()->{WindowManager.setTempCurrentImage(null);if(channels!=null)channels.close();app.dispose();return null;},5);System.exit(0);
    }catch(Throwable error){error.printStackTrace(System.err);System.exit(1);}}
}
'''


def java_value(value) -> str:
    if value is None:
        return "null"
    if type(value) is bool:
        return "true" if value else "false"
    if type(value) is int:
        return str(value)
    if type(value) is float:
        return repr(value)
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=True)
    if isinstance(value, list):
        return "a(" + ",".join(java_value(item) for item in value) + ")"
    if isinstance(value, dict):
        return "o(" + ",".join(java_value(part) for pair in value.items() for part in pair) + ")"
    raise TypeError(type(value))


def pin(path: Path) -> dict:
    return {"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size, "sha256": reference.digest(path)}


def canonical_sha(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def stage_path(path: Path) -> Path:
    resolved = reference._reference_path(path)
    if resolved == STAGE.resolve() or not resolved.is_relative_to(STAGE.resolve()):
        raise ValueError("Composite Max outputs must be fresh children of the owned stage")
    return resolved


def _manifest(value: dict) -> dict:
    return {"members": len(value), "bytes": sum(row["bytes"] for row in value.values()), "canonical_sha256": canonical_sha(value)}


def inputs() -> dict:
    downloads = ROOT / "build/references/downloads"
    archives = ((downloads / f"imagej-{reference.COMMIT}.zip", 6146010, reference.IMAGEJ_SHA),
                (downloads / "fiji-latest-20261007-1617-win64-jdk.zip", 705099776, reference.FIJI_SHA))
    for path, size, sha in archives:
        if path.stat().st_size != size:
            raise ValueError("Pinned archive size differs")
        reference.verified(reference._BoundedFile(path, size, time.monotonic() + 180), sha)
    if reference.digest(RECEIPT) != RECEIPT_SHA:
        raise ValueError("Qualified source/runtime/class receipt changed")
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    if reference.digest(Path(reference.__file__).resolve()) != receipt["reused_helper"]["sha256"]:
        raise ValueError("Qualified bounded helper source changed")
    expected_reference = {"version": "1.54r", "commit": reference.COMMIT,
                          "source_archive_sha256": reference.IMAGEJ_SHA, "runtime_archive_sha256": reference.FIJI_SHA}
    if receipt["reference"] != expected_reference:
        raise ValueError("Qualified class source/runtime binding differs")
    sources = reference._verify_tree(archives[0][0], f"ImageJ-{reference.COMMIT}/", reference.REUSE / "sources", reference.SOURCE_LIMIT)
    runtime = reference._verify_tree(archives[1][0], reference.JDK_PREFIX, reference.REUSE / "jdk", reference.JDK_LIMIT)
    classes = reference._files(REUSED_CLASSES, reference.MAX_BUILD)
    if sources != receipt["verified_source_tree"] or runtime != receipt["verified_runtime_tree"] or classes != receipt["compiled_classes"]:
        raise ValueError("Existing pinned source/runtime/class tree changed; this tool never repairs the shared cache")
    for name, sha in SOURCE_PINS.items():
        if name not in sources or sources[name]["sha256"] != sha:
            raise ValueError("Pinned Max behavioral source differs: " + name)
    return {"reference": expected_reference, "archives": [pin(path) for path, _, _ in archives],
            "reused_receipt": pin(RECEIPT), "source_tree": _manifest(sources), "runtime_tree": _manifest(runtime),
            "reused_classes": _manifest(classes), "known_source_members": {name: sources[name] for name in SOURCE_PINS},
            "helper_sources": [pin(Path(reference.__file__).resolve()), pin(ROOT / "scripts/capture_binary_reference.py")]}


def write_json(path: Path, value) -> None:
    raw = (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    if len(raw) > MAX_PAYLOAD:
        raise ValueError("Reference JSON exceeds 2 MiB")
    with path.open("xb") as stream:
        stream.write(raw)


def payload_size(stage: Path) -> int:
    size = sum(path.stat().st_size for path in stage.rglob("*") if path.is_file()
               and path.suffix in (".json", ".stdout", ".stderr"))
    if size > MAX_PAYLOAD:
        raise ValueError("Combined input/raw/report payload exceeds 2 MiB")
    return size


def run(argv: list[str], stage: Path, label: str, seconds: int) -> dict:
    # Reserve space for the final report or a failure receipt while the helper
    # actively drains stdout/stderr. The helper's cap includes prior logs.
    json_bytes = sum(path.stat().st_size for path in stage.rglob("*.json"))
    reference.MAX_OUTPUT = MAX_PAYLOAD - json_bytes - 64 * 1024
    if reference.MAX_OUTPUT < 1:
        raise ValueError("Frozen inputs leave no bounded process-output budget")
    return reference._run(argv, stage, label, seconds)


def command(stage: Path, prepared: Path, mode: str) -> list[str]:
    return [str(reference.REUSE / "jdk/bin/java.exe"), "-Xmx256m", "-Djava.awt.headless=false",
            "-Duser.home=" + str(stage / "home"), "-Djava.io.tmpdir=" + str(stage / "tmp"),
            "-Dij.dir=" + str(stage), "-cp", os.pathsep.join((str(prepared / "classes"), str(REUSED_CLASSES))), CLASS_NAME, mode]


def _begin(stage: Path, action: str) -> None:
    if stage.exists():
        raise ValueError("Stage must be fresh; preserve every previous attempt")
    stage.mkdir(parents=True, exist_ok=False)
    for name in ("home", "tmp", "tool-sources"):
        (stage / name).mkdir()
    for tool in (Path(__file__).resolve(), Path(reference.__file__).resolve(), ROOT / "scripts/capture_binary_reference.py"):
        shutil.copyfile(tool, stage / "tool-sources" / tool.name)
    write_json(stage / "attempt.json", {"contract": CONTRACT, "action": action, "harness": pin(Path(__file__).resolve()),
                                       "ordered_case_ids": list(CASE_IDS), "state": "attempt_started_no_acceptance"})


def prepare(stage: Path) -> dict:
    stage = stage_path(stage)
    _begin(stage, "prepare")
    processes = {}
    try:
        verified = inputs()
        plan = {"contract": CONTRACT, "ordered_case_ids": list(CASE_IDS), "cases": cases(),
                "ordered_stage_labels": {row["id"]: [entry["label"] for entry in row["actions"]] for row in cases()},
                "stage_count": sum(len(row["actions"]) for row in cases()),
                "output_membership": {"raw_jsonl_rows": 18, "stages": 74, "RGB_readbacks": 62,
                                      "source_before_after_pairs": 22, "real_Channels_dispatches": 8, "one_row_per_case": True,
                                      "per_case_stage_counts": dict(zip(CASE_IDS, STAGE_COUNTS)),
                                      "per_case_RGB_counts": dict(zip(CASE_IDS, RGB_COUNTS)),
                                      "source_before_after_per_fixture": True, "stage_per_declared_action": True,
                                      "RGB_per_stage": "all render=true stages plus Channels dispatch stages; cold nonrender stages explicitly null"},
                "bounds": {"case_ids": 18, "case_seconds": 5, "capture_parent_seconds": CAPTURE_SECONDS,
                           "java_case_loop_seconds": 45, "java_process_seconds": 50, "compile_seconds": 60,
                           "probe_seconds": 20, "java_heap_mib": 256, "combined_payload_bytes": MAX_PAYLOAD,
                           "fresh_stage_bytes": MAX_BUILD, "max_plane_pixels": 20, "max_scalar_samples_per_fixture": 360,
                           "max_scalar_samples_per_case": 456},
                "environment_inputs": {"ImageJ_constructor": "ImageJ.NO_SHOW", "Channels_presentation": "subclass show/setVisible only",
                                       "gui_scale": 1, "auto_contrast": False, "use_inverting_lut": False,
                                       "current_image_binding": "WindowManager.setCurrentWindow for hosted cases and setTempCurrentImage on actual EDT",
                                       "transition_input": "actual Choice.dispatchEvent(ItemEvent.SELECTED, index 4)",
                                       "transition_image_host": {"case_ids": list(HOST_CASE_IDS), "window_class": CLASS_NAME + "$HiddenStackWindow",
                                           "canvas_class": "ij.gui.ImageCanvas", "presentation_override": "show/setVisible only",
                                           "setup_order": list(HOST_SETUP_ORDER), "magnification": 8, "source_rect": "full fixture XY",
                                           "offscreen_surface": "BufferedImage.TYPE_INT_RGB", "paint_method": PAINT_METHOD,
                                           "readback_order": ["operation_or_synchronous_Channels_handler", "updateImage_if_render_true",
                                                              "ImageCanvas.paint_offscreen", "same_image_RGB_readback", "observe_after_state"],
                                           "handler_observation_order": "state_after_handler_before_offscreen_paint_and_RGB_readback",
                                           "cleanup": "after source_after/environment observation, actual StackWindow.close with changes=false"}},
                "scope": "programmatic R01-R11 and real hidden StackWindow/ImageCanvas plus Channels Choice dispatch R12-R18; raw reference only, no product/OS-input/commercial acceptance"}
        write_json(stage / "input-plan.json", plan)
        write_json(stage / "verified-inputs.json", verified)
        classes = stage / "classes"
        (classes / "macros").mkdir(parents=True)
        macro = classes / "macros/InvertAllLuts.txt"
        source_archive = ROOT / "build/references/downloads" / f"imagej-{reference.COMMIT}.zip"
        macro_member = f"ImageJ-{reference.COMMIT}/macros/InvertAllLuts.txt"
        # Copy this one unchanged resource directly from the already verified
        # archive to excluded staging; never embed its implementation here.
        with zipfile.ZipFile(source_archive) as archive:
            macro_bytes = archive.read(macro_member)
        if hashlib.sha256(macro_bytes).hexdigest() != SOURCE_PINS["macros/InvertAllLuts.txt"]:
            raise ValueError("Pinned macro resource bytes changed")
        with macro.open("xb") as stream:
            stream.write(macro_bytes)
        driver = stage / (CLASS_NAME + ".java")
        driver.write_bytes(DRIVER.replace("__CASES__", java_value(cases())).replace("__HOST_ORDER__", java_value(list(HOST_SETUP_ORDER))).encode("utf-8"))
        write_json(stage / "execution-inputs.json", {"driver": pin(driver), "input_plan": pin(stage / "input-plan.json"),
                                                   "verified_inputs": pin(stage / "verified-inputs.json"), "macro_resource": pin(macro),
                                                   "macro_origin": {"archive": pin(source_archive), "member": macro_member,
                                                                    "member_sha256": SOURCE_PINS["macros/InvertAllLuts.txt"]},
                                                   "tools": [pin(path) for path in sorted((stage / "tool-sources").iterdir())]})
        # Only newly authored observer code is compiled. The complete existing
        # reference classes and the macro bytes remain source-receipt bound.
        processes["compile"] = run([str(reference.REUSE / "jdk/bin/javac.exe"), "-J-Xmx256m", "-encoding", "UTF-8",
            "-proc:none", "-implicit:none", "-classpath", str(REUSED_CLASSES), "-d", str(classes), str(driver)], stage, "javac", 60)
        processes["version"] = run([str(reference.REUSE / "jdk/bin/java.exe"), "-Xmx128m", "-version"], stage, "version", 10)
        processes["probe"] = run(command(stage, stage, "--probe"), stage, "probe", 20)
        observed = json.loads((stage / "probe.stdout").read_text(encoding="utf-8"))
        env, choice = observed["environment"], observed["choice"]
        host = observed["state"]["host"]
        if (observed["contract"] != CONTRACT or observed["kind"] != "environment_probe_no_R_dispatch"
                or env["imagej_version"] != "1.54r" or env["awt_headless"] or not env["event_dispatch_thread"]
                or env["gui_scale"] != 1 or env["auto_contrast"] or env["use_inverting_lut"]
                or env["imagej_visible"] or env["imagej_showing"] or choice["visible"] or choice["showing"]
                or choice["items"] != ["Composite", "Color", "Grayscale", "---------", "Composite Max", "Composite Min", "Composite Invert"]
                or not choice["channels_listener_registered"] or choice["handler_declaring_class"] != "ij.plugin.frame.Channels"
                or choice["delivered_item_events"] != 0
                or env["macro_resource"]["sha256"] != SOURCE_PINS["macros/InvertAllLuts.txt"]):
            raise ValueError("Actual hidden Channels wiring/resource probe differs from the declared contract")
        if (not host["open_as_hyperstack"] or not host["displayed_hyperstack"] or not host["window_hyperstack"] or not host["valid_dimensions"]
                or host["position_tcz"] != [1, 1, 2] or host["window_position_czt"] != [1, 2, 1] or host["selector_position_czt"] != [1, 2, 1]
                or host["shape_tczyx"] != [2, 3, 4, 3, 5] or host["current_slice_index"] != 19 or host["stack_size"] != 24
                or not host["current_window_is_host"] or not host["current_image_is_source"]
                or not host["processor_binding"]["pixels_are_source"] or host["window"]["visible"] or host["window"]["showing"]
                or host["canvas"]["magnification"] != 8 or host["canvas"]["source_rect_xywh"] != [0, 0, 5, 3]
                or host["canvas"]["drawing_wh"] != [40, 24] or host["offscreen_paint"]["surface_wh"] != [40, 24]
                or host["offscreen_paint"]["count"] != 1 or host["offscreen_paint"]["last_label"] != "probe"
                or host["offscreen_paint"]["method"] != PAINT_METHOD or observed["host_setup"]["main"]["order"] != list(HOST_SETUP_ORDER)):
            raise ValueError("Actual hidden StackWindow/Canvas probe source/position/paint binding differs")
        if inputs() != verified:
            raise ValueError("Pinned reference inputs changed during preparation")
        report = {"contract": CONTRACT, "state": "prepared_probe_only_R01_R18_not_executed",
                  "harness": pin(Path(__file__).resolve()), "helper": pin(Path(reference.__file__).resolve()),
                  "input_plan": pin(stage / "input-plan.json"), "verified_inputs": pin(stage / "verified-inputs.json"),
                  "execution_inputs": pin(stage / "execution-inputs.json"), "driver": pin(driver),
                  "prepared_classes": reference._files(classes, MAX_BUILD), "processes": processes, "probe": observed}
        write_json(stage / "prepared.json", report)
        payload_size(stage)
        reference._storage(stage)
        return report
    except BaseException as error:
        write_json(stage / "failure.json", {"contract": CONTRACT, "state": "prepare_failed_no_R_credit", "error": repr(error),
                                            "traceback": traceback.format_exc(), "completed_processes": processes})
        raise


def capture(stage: Path, prepared: Path) -> dict:
    started = time.monotonic()
    stage, prepared = stage_path(stage), stage_path(prepared)
    _begin(stage, "capture")
    process = None
    try:
        held = json.loads((prepared / "prepared.json").read_text(encoding="utf-8"))
        if held["harness"] != pin(Path(__file__).resolve()) or held["helper"] != pin(Path(reference.__file__).resolve()):
            raise ValueError("Prepared tools changed; prepare a fresh attempt")
        for key, filename in (("input_plan", "input-plan.json"), ("verified_inputs", "verified-inputs.json"),
                              ("execution_inputs", "execution-inputs.json"), ("driver", CLASS_NAME + ".java")):
            if pin(prepared / filename) != held[key]:
                raise ValueError("Prepared input/source pin changed: " + filename)
        plan = json.loads((prepared / "input-plan.json").read_text(encoding="utf-8"))
        verified = json.loads((prepared / "verified-inputs.json").read_text(encoding="utf-8"))
        if plan["cases"] != cases() or plan["ordered_case_ids"] != list(CASE_IDS):
            raise ValueError("Frozen eighteen inputs/stages changed")
        if reference._files(prepared / "classes", MAX_BUILD) != held["prepared_classes"] or inputs() != verified:
            raise ValueError("Prepared classes or verified reference source/runtime changed")
        # Freeze the consumed input identities in this attempt before Java runs.
        write_json(stage / "execution-inputs.json", {"prepared": pin(prepared / "prepared.json"), "input_plan": held["input_plan"],
                  "verified_inputs": held["verified_inputs"], "driver": held["driver"], "harness": held["harness"],
                  "ordered_case_ids": list(CASE_IDS), "stage_count": plan["stage_count"], "bounds": plan["bounds"]})
        seconds = min(50, int(CAPTURE_SECONDS - (time.monotonic() - started) - 2))
        if seconds < 1:
            raise TimeoutError("Reference preflight consumed the sixty-second parent budget")
        process = run(command(stage, prepared, "--capture"), stage, "reference", seconds)
        raw = stage / "reference.stdout"
        values = [json.loads(line) for line in raw.read_text(encoding="utf-8").splitlines() if line.strip()]
        if tuple(value.get("id") for value in values) != CASE_IDS:
            raise ValueError("Reference did not emit exact ordered R01--R18")
        for value, spec in zip(values, cases()):
            if (value["contract"] != CONTRACT or value["input_contract"] != spec
                    or [entry["input"] for entry in value["stages"]] != spec["actions"]
                    or value["elapsed_ms"] > 5000):
                raise ValueError("Reference omitted a declared input/stage or exceeded its case bound")
            if value["source_before"] != value["source_after"]:
                raise ValueError("Reference display/transition mutated source sample bits")
            for stage_value in value["stages"]:
                action = stage_value["input"]
                if (stage_value["rgb"] is not None) != (action["render"] or action["operation"] == "channels_choice"):
                    raise ValueError("Declared RGB output/stage cardinality differs")
                if action["operation"] == "channels_choice" and stage_value["dispatch"] is None:
                    raise ValueError("A transition lacks real Channels dispatch observations")
        if reference._files(prepared / "classes", MAX_BUILD) != held["prepared_classes"] or inputs() != verified:
            raise ValueError("Pinned input bytes changed during capture")
        if time.monotonic() - started > CAPTURE_SECONDS:
            raise TimeoutError("Reference capture parent exceeded sixty seconds")
        report = {"contract": CONTRACT, "state": "raw_reference_observations_only_no_product_or_parity_acceptance",
                  "prepared": pin(prepared / "prepared.json"), "execution_inputs": pin(stage / "execution-inputs.json"),
                  "raw": pin(raw), "process": process, "harness": pin(Path(__file__).resolve()),
                  "ordered_case_ids": list(CASE_IDS), "case_count": len(values), "stage_count": sum(len(row["stages"]) for row in values),
                  "elapsed_seconds": round(time.monotonic() - started, 3), "scope": plan["scope"],
                  "policy_decisions": [], "commercial_eligibility": "not_decided_by_capture_tool"}
        write_json(stage / "capture.json", report)
        payload_size(stage)
        reference._storage(stage)
        return report
    except BaseException as error:
        write_json(stage / "failure.json", {"contract": CONTRACT, "state": "capture_failed_not_numerical_or_product_evidence",
                  "error": repr(error), "traceback": traceback.format_exc(), "completed_process": process,
                  "elapsed_seconds": round(time.monotonic() - started, 3)})
        raise


def main() -> None:
    # The existing bounded process drainer stops at this tighter packet cap.
    # These are process-local settings; no shared cache/helper file is changed.
    reference.MAX_OUTPUT = MAX_PAYLOAD
    reference.MAX_BUILD = MAX_BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--prepare", action="store_true", help="Compile and probe hidden Channels; never execute R cases")
    action.add_argument("--capture", action="store_true", help="Explicit R01--R18 execution after root freeze")
    parser.add_argument("--stage", type=Path)
    parser.add_argument("--prepared", type=Path, default=STAGE / "prepare-001")
    args = parser.parse_args()
    stage = args.stage or STAGE / ("prepare-001" if args.prepare else "capture-001")
    report = prepare(stage) if args.prepare else capture(stage, args.prepared)
    print(json.dumps({"state": report["state"], "stage": stage_path(stage).relative_to(ROOT).as_posix(),
                      "report": "prepared.json" if args.prepare else "capture.json"}))


if __name__ == "__main__":
    main()

"""Portable public parity against independently qualified ImageJ240 captures.

Python creates literal inputs and checks typed hashes only; all processing is
native. ImageJ positive scalar spacing is separate from native owned affine
geometry and copied reference pixel-offset origins.
"""
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np
import pytest

PROFILES = ('down_noninteger','up_noninteger','equal_dimension_crop','integer_quad_ties','identity_no_resize')
DEPTHS = (.75,1.5,1.125,4.,1.)
KINDS = ('u8','u16','f32')
PLANES = ('xz','yz')
CASE_IDS = tuple(f'{kind}.{plane}.{profile}.reverse{int(bool(flags&4))}.flip{int(bool(flags&2))}.transpose{int(bool(flags&1))}'
    for profile in PROFILES for kind in KINDS for plane in PLANES for flags in range(8))
PREPARED_IDS = tuple(case_id for case_id in CASE_IDS if
    ('.down_noninteger.reverse1.flip1.transpose1' in case_id or '.identity_no_resize.reverse0.flip0.transpose0' in case_id))
CAPTURE_SHA = '9767bb75707acc21ca4e21b0f66ce06ebcf2b7fbc42db404fc141d14c6aeb142'
COLD_SHA = 'c7959197af51681055c639f5f4a831dd93c55877a78e159c01a46d3e5a2b15a6'
RECIPE_SHA = 'b5efcc5635e74625d5c7be9b2014471ffc169e902e29a60905c1e0f14d7e42c4'
ORACLE_SHA = '96e9b896bf584492e551baeb72e4f04ff435d01aec69b4082dee652de7f509b9'


@pytest.fixture(autouse=True)
def public_api():
    global ij
    import imagejxx as ij


@pytest.fixture(scope='module')
def qualified_reference():
    path = Path(__file__).resolve().parents[1]/'fixtures/slicer-bilinear-imagej154r.json'
    assert path.is_file() and not path.is_symlink() and path.stat().st_size<=2*1024**2
    def pairs(items):
        value = {}
        for key,item in items:
            assert key not in value
            value[key] = item
        return value
    def invalid(token):
        raise AssertionError('Nonfinite fixture metadata: '+token)
    fixture = json.loads(path.read_text(encoding='utf-8'),object_pairs_hook=pairs,parse_constant=invalid)
    json.dumps(fixture,allow_nan=False)
    assert fixture['schema_version']==1 and fixture['format']=='qualified_imagej_slicer_bilinear240_v1'
    assert fixture['case_count']==240 and tuple(fixture['case_ids'])==CASE_IDS
    assert tuple(row['case_id'] for row in fixture['cases'])==CASE_IDS
    reference = fixture['reference']
    assert (reference['imagej_version'],reference['revision'])==('1.54r','62db90a269cccc1ac79f658e4602043214ed055b')
    assert reference['capture']['sha256']==CAPTURE_SHA and reference['cold_receipt']['sha256']==COLD_SHA
    assert reference['original_input_recipe']['sha256']==RECIPE_SHA and reference['independent_oracle_source']['sha256']==ORACLE_SHA
    assert reference['origin_counts']=={'source':519,'classpath':472,'jdk':581,'driver_classes':2}
    assert fixture['scope']['source_tczyxs']==[1,1,7,5,9,1]
    assert fixture['scope']['imagej_pixel_offset_origins_are_native_affine_goldens'] is False
    assert fixture['scope']['native_full_affine_or_world_equality'] is False
    return {row['case_id']:row for row in fixture['cases']}


def bytes_le(value):
    value = np.asarray(value)
    return np.ascontiguousarray(value.astype(value.dtype.newbyteorder('<'),copy=False)).tobytes()


def digest(value):
    return hashlib.sha256(bytes_le(value)).hexdigest()


def scalar_bits(values):
    return [struct.pack('>d',float(value)).hex() for value in values]


def literal_source(kind):
    """Input-only law copied from the sealed independent input annex, no resize."""
    z,y,x = np.indices((7,5,9),dtype=np.int64)
    edge = (x==8)|(y==4)|(z==6)
    parity = (x+y+z)&1
    if kind=='u8':
        values = np.where(edge,255,parity).astype(np.uint8)
    elif kind=='u16':
        values = np.where(edge,65535,32768+parity).astype(np.uint16)
    else:
        assert kind=='f32'
        interior = (13*x-29*y+47*z+19*x*y-311)*128+.125
        boundary = np.where(parity==0,1.,-1.)*(1000000+13*x+29*y+47*z+.125)
        values = np.where(edge,boundary,interior).astype(np.float32)
    return values.reshape(1,1,7,5,9,1)


def source(row):
    input = row['input']
    kind,plane,profile,reverse,flip,transpose = row['case_id'].split('.')
    flags = tuple(value.endswith('1') for value in (reverse,flip,transpose))
    assert kind in KINDS and plane in PLANES and profile in PROFILES
    assert (input['type'],input['plane'],input['profile'])==(kind,plane,profile)
    assert input['source_shape_tczyxs']==[1,1,7,5,9,1]
    assert (input['pixel_width_argument'],input['pixel_height_argument'],input['pixel_depth_argument'])==(1.,2.25,DEPTHS[PROFILES.index(profile)])
    assert (input['first_zero_based'],input['last_inclusive_zero_based'],input['positive_step'])==(1,4 if plane=='xz' else 8,2)
    assert (input['reverse_order'],input['flip_vertical'],input['transpose'])==flags
    values = literal_source(kind)
    assert digest(values)==row['source_typed_sha256']
    # Reference origins are pixel offsets, not a native physical affine origin.
    cal = ij.SpatialCalibration(spacing=(1.,2.25,input['pixel_depth_argument']),unit='um',frame_interval=.75,time_unit='s')
    return values,ij.Dataset(values,axes='TCZYXS',calibration=cal)


def request(row):
    value = row['input']
    return ij.AxisRangeSlicerResizeRequest(ij.AxisRangeOrientationRequest(
        ij.AxisRangeResliceRequest(value['plane'],value['first_zero_based'],value['last_inclusive_zero_based'],value['positive_step']),
        reverse_order=value['reverse_order'],flip_vertical=value['flip_vertical'],transpose=value['transpose']))


def cal_signature(value):
    return (tuple(value.spacing),tuple(value.origin),tuple(value.direction),value.unit,value.frame_interval,
            value.time_origin,value.time_unit)


def checked_output(dataset,row):
    value = dataset.numpy('TCZYXS')
    assert tuple(dataset.shape)==tuple(row['output_shape_tczyxs'])==value.shape
    assert value.dtype==np.dtype({'u8':np.uint8,'u16':np.uint16,'f32':np.float32}[row['input']['type']])
    assert len(bytes_le(value))==row['output_byte_count']
    assert re.fullmatch('[0-9a-f]{64}',row['output_typed_sha256'])
    assert digest(value)==row['output_typed_sha256']


@pytest.mark.parametrize('case_id',CASE_IDS)
def test_qualified_imagej240_typed_pixels_estimate_and_scalar_spacing(qualified_reference,case_id):
    row = qualified_reference[case_id]
    values,dataset = source(row)
    before,before_cal = bytes_le(dataset.numpy('TCZYXS')),cal_signature(dataset.calibration)
    req = request(row)
    estimate = ij.estimate_axis_range_slicer_resize(dataset,req)
    assert bytes_le(dataset.numpy('TCZYXS'))==before
    result = ij.reslice_slicer_axis_range(dataset,req)
    checked_output(result.dataset,row)
    shape = row['output_shape_tczyxs']
    selected = row['selected_source_coordinates']
    assert (estimate.width,estimate.height,estimate.plane_count,estimate.samples)==(shape[4],shape[3],shape[2],1)
    assert estimate.source_vertical_count==result.source_vertical_count==7
    assert estimate.vertical_count==result.vertical_count==(shape[4] if row['input']['transpose'] else shape[3])
    assert (estimate.last_selected,estimate.effective_first,estimate.effective_last)==(max(selected),selected[0],selected[-1])
    branch = {'no_resize':'NoResize','equal_dimension_crop':'EqualDimensionCrop','bilinear_resize':'Bilinear'}[row['resize_branch']]
    assert estimate.resize_branch==result.resize_branch==getattr(ij.AxisRangeSlicerResizeBranch,branch)
    assert estimate.working_bytes==result.working_bytes
    assert scalar_bits(estimate.scalar_spacing)==scalar_bits(result.scalar_spacing)==row['scalar_spacing_float64_hex']
    assert scalar_bits(row['scalar_spacing'])==row['scalar_spacing_float64_hex']
    assert not result.dataset.shares_pixels(dataset)
    assert bytes_le(dataset.numpy('TCZYXS'))==before==bytes_le(values)
    assert cal_signature(dataset.calibration)==before_cal
    # Full affine/world offsets and fixed-axis stride geometry have separate native
    # literal tests; no equality with ImageJ's copied pixel-offset calibration here.


def owner_state(owner):
    snap = owner.snapshot()
    return (bytes_le(snap.dataset.numpy('TCZYXS')),cal_signature(snap.dataset.calibration),tuple(snap.dataset.shape),
        snap.metadata,snap.record_layout,snap.data_identity,snap.dirty,owner.undo_steps,owner.redo_steps,
        (snap.revisions.pixels,snap.revisions.metadata,snap.revisions.selection,snap.revisions.display),
        snap.selection,snap.previous_selection,snap.rois,snap.overlays,snap.spectrum)


@pytest.mark.parametrize('case_id',PREPARED_IDS)
def test_representative_captured_request_first_fresh_owner_publication(qualified_reference,case_id):
    row = qualified_reference[case_id]
    _,dataset = source(row)
    app = ij.Application()
    owner,other = ij.Document(dataset,'Reference source'),ij.Document(dataset,'Other')
    app.adopt(owner);app.adopt(other);app.activate(owner.id)
    annotation = owner.begin_edit()
    annotation.set_property('source_only',[1,2])
    annotation.set_extension('source_extension',{'kept':True})
    annotation.set_record_layout({'source_layout':[1]})
    owner.commit(annotation)
    before,other_before = owner_state(owner),owner_state(other)
    ids = tuple(app.document_ids)
    req = request(row)
    callback_events = []
    def change_caller(done,total):
        callback_events.append((done,total))
        if len(callback_events)==1:
            orientation = req.orientation
            range_value = orientation.range
            range_value.plane = 'yz' if row['input']['plane']=='xz' else 'xz'
            range_value.first = range_value.last = 0
            range_value.step = 1
            orientation.range = range_value
            orientation.reverse_order = not orientation.reverse_order
            orientation.flip_vertical = not orientation.flip_vertical
            orientation.transpose = not orientation.transpose
            req.orientation = orientation
            app.activate(other.id)
    value = app.prepare_axis_range_slicer_resize(req,owner.id,progress=change_caller)
    assert callback_events and tuple(app.document_ids)==ids and app.active_document_id==other.id
    assert owner_state(owner)==before and owner_state(other)==other_before
    candidate = value.prepared_new_document()
    assert candidate.id not in ids and candidate.id>0 and not app.contains(candidate.id)
    snap = candidate.snapshot()
    checked_output(snap.dataset,row)
    assert snap.data_identity==0 and snap.dirty is False and candidate.undo_steps==candidate.redo_steps==0
    assert snap.selection is snap.previous_selection is snap.spectrum is None
    assert snap.rois==snap.overlays==[] and snap.record_layout=={} and snap.metadata['properties']=={}
    assert set(snap.metadata['extensions'])=={'axis_range_slicer_resize'}
    original = row['input']
    for name in ('first','last','step'):
        expected = original[{'first':'first_zero_based','last':'last_inclusive_zero_based','step':'positive_step'}[name]]
        assert value.parameters[name]==expected
    assert value.parameters['plane']==original['plane'].upper()
    for name in ('reverse_order','flip_vertical','transpose'):
        assert value.parameters[name] is original[name]
    assert scalar_bits(value.parameters['scalar_spacing'])==row['scalar_spacing_float64_hex']
    assert snap.metadata['extensions']['axis_range_slicer_resize']['source_document_id']==owner.id
    publication = app.publish(value)
    assert publication.document is candidate and app.contains(candidate.id) and app.active_document_id==candidate.id
    assert set(app.document_ids)==set(ids)|{candidate.id} and len(app.document_ids)==len(ids)+1 and not value.active
    checked_output(candidate.snapshot().dataset,row)
    with pytest.raises(ValueError):
        app.publish(value)
    assert set(app.document_ids)==set(ids)|{candidate.id} and len(app.document_ids)==len(ids)+1
    assert app.active_document_id==candidate.id
    assert owner_state(owner)==before and owner_state(other)==other_before

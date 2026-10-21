"""One shared frozen guard, explicit pairs/names, complete canonical ZIP output."""
import base64
import gc
import io
import json
import weakref
import zipfile
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_roi_import import owner,bare,envelope,archive,EXTENT,ROWS,SOURCES


def projection(row,group=9):
    value=ij.ImageJRoi(ij.ROI(row.name,row.selection.with_attributes({}),t=row.t,c=row.c,z=row.coordinate))
    value.group=group;value.properties='zip projection: λ😀\n';return value


def fixture():
    doc,reads=owner()
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[bare(),envelope()])))
    mask=np.zeros((6,8),dtype=np.uint8);mask[0,0]=mask[-1,-1]=1
    rows=[
        ij.PlanarAnnotation('XY',ij.Selection('rectangle',[(1.250000000001,2.500000000001),(3.750000000001,4.500000000001)]),'xy',1,t=1,c=2,attributes={'plane':'xy'}),
        ij.PlanarAnnotation('XZ λ😀',ij.Selection('point',[(7.250000000001,.500000000001)],attributes={'opaque':[None,2**64-1]}),'xz',4,t=None,c=1,attributes={'plane':'xz'}),
        ij.PlanarAnnotation('YZ mask',ij.Selection.from_mask(mask),'yz',3,t=0,c=None,attributes={'plane':'yz'}),
    ]
    for i,row in enumerate(rows):
        tx=ij.prepare_update_planar_annotation(doc.snapshot(),i,row) if i<2 else ij.prepare_store_planar_annotation(doc.snapshot(),row)
        doc.commit(tx)
    items=[ij.ImageJRoiZipExportItem(name,source,i,projection(row,9+i))
           for i,(name,source,row) in enumerate(zip(['XY.roi','XZ-λ😀##.roi','mask.roi'],[1,0,1],rows))]
    return doc,reads,rows,items


@pytest.mark.parametrize('compress',[True,False])
def test_complete_mixed_planes_exact_native_rows_source_basis_and_zip(compress):
    doc,reads,rows,items=fixture();before=doc.snapshot();history=doc.undo_steps
    result=ij.resolve_imagej_roi_zip_export(before,items,compress=compress);record=result.record()
    assert set(record)=={'schema','version','source','limits','annotations','sources','members','compress','output'}
    assert record['schema']=='imagejxx.imagej-roi-zip-export' and record['version']==1 and record['compress']==compress
    assert record['sources']==archive(doc) and record['annotations']==before.metadata['extensions'][ROWS]
    assert [set(v) for v in record['members']]==[{'name','source_index','annotation_index','projection'}]*3
    assert 'pairing' not in record and json.dumps(record).count('imagejxx.imagej-roi-import-sources')==1
    assert base64.b64decode(record['output'])==result.data
    with zipfile.ZipFile(io.BytesIO(result.data)) as transport:
        assert transport.namelist()==[i.name for i in items]
        assert [i.compress_type for i in transport.infolist()]==[zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED]*3
        for member,row,item in zip(result.members,rows,items):
            wire=transport.read(member.name);assert member.data==wire
            decoded=ij.decode_imagej_roi_native_envelope(wire)
            assert decoded.annotation.selection.points==row.selection.points
            if row.selection.kind=='mask':np.testing.assert_array_equal(decoded.annotation.selection.mask((6,8)),row.selection.mask((6,8)))
            assert decoded.annotation.selection.attributes==row.selection.attributes
            assert decoded.annotation.attributes==row.attributes
            assert (decoded.annotation.plane,decoded.annotation.coordinate,decoded.annotation.t,decoded.annotation.c)==(row.plane,row.coordinate,row.t,row.c)
            assert decoded.source_record.group==7 and decoded.source_record.properties=='source: λ😀\n'
            width,height={'xy':(12,8),'xz':(12,6),'yz':(8,6)}[row.plane]
            assert ij.decode_imagej_roi(wire,mask_width=width,mask_height=height).group==item.standard_projection.group
    ij.validate_imagej_roi_zip_export_record(record)
    assert ij.imagej_roi_zip_export_from_record(before,record).data==result.data
    request=ij.decode_imagej_roi_zip_export_request(record)
    assert [v.name for v in request.items]==[v.name for v in items] and request.compress==compress
    assert ij.resolve_imagej_roi_zip_export(before,request.items,request.limits,request.compress).data==result.data
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history and reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('name',['../a.roi','a/b.roi','a\\b.roi','CON.roi','a.ROI','.roi','x\x00.roi','a?.roi','a\n.roi','x'*1025+'.roi'])
def test_flat_portable_member_names(name):
    _,_,rows,_=fixture()
    with pytest.raises((ValueError,UnicodeError)):ij.ImageJRoiZipExportItem(name,0,0,projection(rows[0]))


@pytest.mark.parametrize('failure',['duplicate','late-source','late-row','late-projection','empty'])
def test_late_or_ambiguous_member_failure_is_atomic(failure):
    doc,reads,rows,items=fixture();before=doc.snapshot();history=doc.undo_steps
    if failure=='duplicate':
        items[-1]=ij.ImageJRoiZipExportItem('XY.roi',1,2,projection(rows[2]))
    elif failure=='late-source':items[-1]=ij.ImageJRoiZipExportItem('last.roi',999,2,projection(rows[2]))
    elif failure=='late-row':items[-1]=ij.ImageJRoiZipExportItem('last.roi',0,999,projection(rows[2]))
    elif failure=='late-projection':items[-1]=ij.ImageJRoiZipExportItem('last.roi',0,2,projection(rows[0]))
    else:items=[]
    with pytest.raises(ValueError):ij.resolve_imagej_roi_zip_export(before,items)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history and reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('path,value',[
    (('version',),True),(('version',),2),(('destination',),'must-not-be-used.zip'),(('compress',),1),
    (('members',1,'source_index'),True),(('members',2,'annotation_index'),999),
    (('members',2,'name'),'XY.roi'),(('members',0,'extra'),None),
    (('members',2,'projection'),'AAAA'),(('sources','entries',1,'data'),'AAAA'),
    (('output',),'AAAA'),(('limits','zip','max_entries'),0),(('limits','output','max_record_bytes'),1),
])
def test_closed_record_and_complete_equation_tampering(path,value):
    doc,reads,rows,items=fixture();record=ij.resolve_imagej_roi_zip_export(doc.snapshot(),items).record();target=record
    for key in path[:-1]:target=target[key]
    target[path[-1]]=value
    with pytest.raises((ValueError,TypeError)):ij.validate_imagej_roi_zip_export_record(record)
    with pytest.raises((ValueError,TypeError)):ij.decode_imagej_roi_zip_export_request(record)
    with pytest.raises((ValueError,TypeError)):ij.rebind_imagej_roi_zip_export(doc.snapshot(),record,items)
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('kind',['input','projection','member','aggregate','archive','working','record','count'])
def test_aggregate_resource_limits(kind):
    doc,reads,rows,items=fixture()
    values={}
    if kind=='input':values['sources']=ij.ImageJRoiImportLimits(max_decoded_bytes=1024)
    if kind=='projection':values['output']=ij.ImageJRoiExportLimits(roi=ij.ImageJRoiLimits(max_bytes=64))
    if kind=='record':values['output']=ij.ImageJRoiExportLimits(max_record_bytes=1000)
    zip_values={'member':{'max_entry_bytes':64},'aggregate':{'max_total_bytes':1000},'archive':{'max_archive_bytes':22},
                'working':{'max_working_bytes':1024},'count':{'max_entries':2}}
    if kind in zip_values:values['zip']=ij.ImageJRoiZipLimits(**zip_values[kind])
    with pytest.raises(ValueError):ij.resolve_imagej_roi_zip_export(doc.snapshot(),items,ij.ImageJRoiZipExportLimits(**values))
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_replay_guards_display_only_and_explicit_rebind_session():
    doc,reads,rows,items=fixture();before=doc.snapshot();result=ij.resolve_imagej_roi_zip_export(before,items);record=result.record()
    view=before.display;view.t=1;doc.set_display(view)
    assert ij.imagej_roi_zip_export_from_record(doc.snapshot(),record).data==result.data
    other=ij.Document(before)
    with pytest.raises(ValueError,match='another source'):ij.imagej_roi_zip_export_from_record(other.snapshot(),record)
    assert ij.rebind_imagej_roi_zip_export(other.snapshot(),record,items).data==result.data
    tx=doc.begin_edit();tx.set_title('changed');doc.commit(tx)
    with pytest.raises(ij.RevisionConflict):ij.imagej_roi_zip_export_from_record(doc.snapshot(),record)
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    session=ij.decode_session(ij.encode_session(ij.SessionCapture([other],workflow=[record])))
    assert session.workflow==[record]
    assert ij.rebind_imagej_roi_zip_export(session.documents[0].snapshot(),record,items).data==result.data


def test_original_history_precedes_bad_member_and_request_inputs_are_copies():
    doc,_,rows,items=fixture();record=ij.resolve_imagej_roi_zip_export(doc.snapshot(),items).record()
    bad=dict(record);bad['sources']=dict(record['sources']);bad['sources']['entries']=[dict(v) for v in record['sources']['entries']]
    bad['sources']['entries'][-1]['data']='AAAA';bad['members']=[dict(v) for v in record['members']];bad['members'][0]['source_index']=999
    with pytest.raises(ValueError) as error:ij.validate_imagej_roi_zip_export_record(bad)
    assert 'index out of range' not in str(error.value)
    request=ij.decode_imagej_roi_zip_export_request(record);projection_copy=request.items[0].standard_projection;projection_copy.group=200
    assert request.items[0].standard_projection.group==9


def test_member_views_keep_owner_alive_without_copying_entire_batch():
    doc,_,rows,items=fixture();result=ij.resolve_imagej_roi_zip_export(doc.snapshot(),items)
    owner_ref=weakref.ref(result);member=result.members[0];expected=member.data;del result;gc.collect()
    assert owner_ref() is not None and member.data==expected and member.mapped.annotation.name==rows[0].name
    del member;gc.collect();assert owner_ref() is None


@pytest.mark.parametrize('value',[True,1.0,'0',-1,2**100])
def test_strict_indices_and_compression(value):
    doc,_,rows,items=fixture()
    with pytest.raises((TypeError,ValueError,OverflowError)):ij.ImageJRoiZipExportItem('x.roi',value,0,projection(rows[0]))
    with pytest.raises((TypeError,ValueError,OverflowError)):ij.ImageJRoiZipExportItem('x.roi',0,value,projection(rows[0]))
    if value is not True:
        with pytest.raises(TypeError):ij.resolve_imagej_roi_zip_export(doc.snapshot(),items,compress=value)


def test_source_capture_and_record_are_shared_across_many_members():
    doc,reads=owner();first=bare('first.roi');doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[first])))
    source=envelope();old=ij.PlanarAnnotation('old',ij.Selection('point',[(1,1)]),'xy',0,attributes={'payload':'x'*65536})
    choice=ij.ImageJRoiNativeReplacement(EXTENT,old)
    for i in range(10):
        item=ij.ImageJRoiImportItem(f'history-{i}.roi',source.data,native_replacement=choice)
        doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item])))
    tx=doc.begin_edit();native=doc.snapshot().metadata['extensions'][ROWS];native['rows']=native['rows'][:1];tx.set_extension(ROWS,native);doc.commit(tx)
    row=ij.planar_annotations(doc.snapshot())[0]
    items=[ij.ImageJRoiZipExportItem(f'copy-{i}.roi',0,0,projection(row)) for i in range(16)]
    limits=ij.ImageJRoiZipExportLimits(sources=ij.ImageJRoiImportLimits(max_decoded_bytes=128*1024),output=ij.ImageJRoiExportLimits(max_record_bytes=2*1024*1024))
    result=ij.resolve_imagej_roi_zip_export(doc.snapshot(),items,limits);record=result.record()
    assert len(result.members)==16 and len(record['sources']['entries'])==11
    assert all(set(v)=={'name','source_index','annotation_index','projection'} for v in record['members'])
    assert json.dumps(record).count('imagejxx.imagej-roi-import-sources')==1 and len(json.dumps(record))<2*1024*1024
    ij.validate_imagej_roi_zip_export_record(record)
    assert ij.imagej_roi_zip_export_from_record(doc.snapshot(),record).data==result.data
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_case_distinct_names_preserve_exact_archive_identity():
    doc,_,rows,items=fixture();items[-1]=ij.ImageJRoiZipExportItem('xy.roi',1,2,projection(rows[2]))
    result=ij.resolve_imagej_roi_zip_export(doc.snapshot(),items)
    assert [v.name for v in ij.decode_imagej_roi_zip(result.data)]==['XY.roi','XZ-λ😀##.roi','xy.roi']


def test_exact_record_budget_and_canonical_request_retention_boundary():
    doc,_,rows,items=fixture();source=doc.snapshot()
    def caps(value):return ij.ImageJRoiZipExportLimits(sources=ij.ImageJRoiImportLimits(max_decoded_bytes=value))
    lo,hi=1024,128*1024
    while lo<hi:
        middle=(lo+hi)//2
        try:ij.resolve_imagej_roi_zip_export(source,items,caps(middle))
        except ValueError:lo=middle+1
        else:hi=middle
    result=ij.resolve_imagej_roi_zip_export(source,items,caps(lo));record=result.record()
    ij.validate_imagej_roi_zip_export_record(record)
    request=ij.decode_imagej_roi_zip_export_request(record)
    assert ij.resolve_imagej_roi_zip_export(source,request.items,request.limits).data==result.data
    def storage(v):
        if isinstance(v,dict):return 32+sum(len(k.encode())+storage(value) for k,value in v.items())
        if isinstance(v,list):return 32+sum(map(storage,v))
        return 32+(len(v.encode()) if isinstance(v,str) else 0)
    bound=sum(len(k.encode())+storage(v) for k,v in record.items())
    limits=ij.ImageJRoiZipExportLimits(sources=request.limits.sources,output=ij.ImageJRoiExportLimits(max_record_bytes=bound))
    exact=ij.resolve_imagej_roi_zip_export(source,request.items,limits);ij.validate_imagej_roi_zip_export_record(exact.record())
    limits=ij.ImageJRoiZipExportLimits(sources=request.limits.sources,output=ij.ImageJRoiExportLimits(max_record_bytes=bound-1))
    with pytest.raises(ValueError):ij.resolve_imagej_roi_zip_export(source,request.items,limits)


def test_custom_sequence_is_snapshotted_once_before_owned_native_copies():
    doc,_,rows,items=fixture();calls=[0]*len(items)
    class Once:
        def __len__(self):return len(items)
        def __getitem__(self,i):
            calls[i]+=1
            assert calls[i]==1
            return items[i]
    result=ij.resolve_imagej_roi_zip_export(doc.snapshot(),Once())
    assert calls==[1,1,1] and len(result.members)==3

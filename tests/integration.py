#!/usr/bin/env python3
"""Exercise the real OpenGL renderer and thumbnail lifecycle in an isolated cache."""
import hashlib, json, os, pathlib, struct, subprocess, sys, tempfile, zlib
binary = pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'build/stl-folder-preview').resolve()

def stl(path, count=1):
    face=struct.pack('<12fH',0,0,1,0,0,0,1,0,0,0,1,0,0)
    path.write_bytes(b'\0'*80+struct.pack('<I',count)+face*count)

def png_info(path):
    raw=path.read_bytes();assert raw[:8]==b'\x89PNG\r\n\x1a\n'
    offset=8; metadata={}; pixels=b''; size=None
    while offset<len(raw):
        length=struct.unpack('>I',raw[offset:offset+4])[0]; kind=raw[offset+4:offset+8];chunk=raw[offset+8:offset+8+length];offset+=12+length
        if kind==b'IHDR':size=struct.unpack('>II',chunk[:8])
        if kind==b'tEXt':
            key,value=chunk.split(b'\0',1); metadata[key.decode()]=value.decode()
        if kind==b'zTXt':
            key,value=chunk.split(b'\0',1);metadata[key.decode()]=zlib.decompress(value[1:]).decode()
        if kind==b'iTXt':
            key,rest=chunk.split(b'\0',1); compressed=rest[0]; rest=rest[2:];lang,rest=rest.split(b'\0',1);translated,value=rest.split(b'\0',1)
            metadata[key.decode()]=(zlib.decompress(value) if compressed else value).decode()
        if kind==b'IDAT':pixels+=chunk
    assert len(set(zlib.decompress(pixels)))>20,'Blank render'
    return size,metadata

with tempfile.TemporaryDirectory(prefix='stl-preview-test-') as root:
    root=pathlib.Path(root);folder=root/'models with spaces';folder.mkdir();other=root/'other';other.mkdir()
    env=os.environ.copy();env.update(XDG_CACHE_HOME=str(root/'cache'),XDG_CONFIG_HOME=str(root/'config'),QT_QPA_PLATFORM='xcb')
    config=root/'config'/'JoeMirai'/'stl-folder-preview.conf';config.parent.mkdir(parents=True)
    config.write_text('[General]\ntriangles=10\nresolution=1\n')
    model=folder/'one triangle.STL';stl(model);stl(folder/'too-many.stl',11)
    (folder/'bad.stl').write_text('invalid')
    with (folder/'large.stl').open('wb') as f:f.truncate(31*1024*1024)
    sub=folder/'nested';sub.mkdir();stl(sub/'nested.stl')
    def run(directory):subprocess.run([str(binary),'--render-and-exit',str(directory)],env=env,check=True,timeout=60)
    run(folder)
    index=root/'cache'/'JoeMirai'/'stl-folder-preview'/'index.json'
    # Qt CacheLocation includes organization/application.
    if not index.exists(): index=next((root/'cache').rglob('index.json'))
    manifest=json.loads(index.read_text());assert list(manifest)==[str(model)],manifest
    image=root/'cache'/'thumbnails'/'large'/(hashlib.md5(model.as_uri().encode()).hexdigest()+'.png')
    size,metadata=png_info(image);assert size==(512,512),size
    assert metadata['Thumb::URI']==model.as_uri();assert metadata['Software']=='stl-folder-preview'
    # Rerender in wireframe at a different resolution.
    config.write_text('[General]\ntriangles=10\nresolution=0\nmode=1\n')
    run(folder);assert png_info(image)[0]==(256,256)
    model.unlink();assert image.exists(),'Cleanup must wait for another render'
    # A foreign cache entry must survive cleanup.
    foreign=root/'cache'/'thumbnails'/'large'/'foreign.png';foreign.write_bytes(b'foreign')
    run(other);assert not image.exists();assert foreign.exists();assert json.loads(index.read_text())=={}
    print('PASS: rendering, PNG metadata, limits, uppercase extension, folder scope, rerender and on-demand cleanup')

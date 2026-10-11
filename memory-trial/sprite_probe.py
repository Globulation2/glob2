# Read-only, parent-process inspection; offsets come from the actual compiled headers.
import struct,json,pathlib,re,collections
LAYOUT=json.loads((pathlib.Path(__file__).parent/'sprite_offsets.json').read_text())
def capture(pid,registry_address):
 L=LAYOUT; groups=collections.defaultdict(lambda:{'cpu_pixel_bytes':0,'gpu_texture_bytes':0,'cpu_mip_bytes':0,'cpu_upload_bytes':0,'surfaces':0});seen_sprites=set();seen_surfaces=set();seen_sdl=set()
 with open(f'/proc/{pid}/mem','rb',buffering=0) as memory:
  def read(addr,size):
   if addr<4096 or size<0 or size>5000000:raise ValueError('invalid remote range')
   memory.seek(addr);raw=memory.read(size)
   if len(raw)!=size:raise ValueError('short remote read')
   return raw
  def u64(addr):return struct.unpack('<Q',read(addr,8))[0]
  def ptr(raw,offset=0):return struct.unpack_from('<Q',raw,offset)[0]
  def vector(raw,offset):
   start,end,capacity=struct.unpack_from('<QQQ',raw,offset)
   if not (start<=end<=capacity) or end-start>8*50000:raise ValueError('invalid vector')
   return struct.unpack('<'+'Q'*((end-start)//8),read(start,end-start)) if end>start else []
  def tree(container):
   root=u64(container+L['rb_root']);pending=[root];visited=set()
   while pending:
    node=pending.pop()
    if not node or node in visited:continue
    if len(visited)>50000:raise ValueError('invalid tree')
    visited.add(node);raw=read(node,L['rb_value']);pending.extend([ptr(raw,L['rb_left']),ptr(raw,L['rb_right'])]);yield node+L['rb_value']
  def name(raw):
   address,length=struct.unpack_from('<QQ',raw,L['filename'])
   if length>1024:raise ValueError('invalid string')
   return read(address,length).decode(errors='replace') if length else 'unnamed sprite'
  def surface(address,owner):
   if not address or address in seen_surfaces:return
   raw=read(address,L['surface_size']);seen_surfaces.add(address)
   sdl=ptr(raw,L['sdl_surface']);cpu=0
   if sdl and sdl not in seen_sdl:
    sr=read(sdl,L['sdl_size']);height=struct.unpack_from('<i',sr,L['sdl_h'])[0];pitch=struct.unpack_from('<i',sr,L['sdl_pitch'])[0]
    if not (0<=height<=100000 and 0<=pitch<=1000000):raise ValueError('invalid surface')
    cpu=height*pitch;seen_sdl.add(sdl)
   g=groups[owner];g['cpu_pixel_bytes']+=cpu;g['gpu_texture_bytes']+=ptr(raw,L['gpu_bytes']);g['surfaces']+=1
   begin,end=struct.unpack_from('<QQ',raw,L['upload_pixels'])
   if not 0<=end-begin<=1024**3:raise ValueError('invalid upload buffer')
   g['cpu_upload_bytes']+=end-begin
   begin,end=struct.unpack_from('<QQ',raw,L['mips'])
   if not 0<=end-begin<=32*L['mip_size'] or (end-begin)%L['mip_size']:raise ValueError('invalid mip vector')
   for item in range(begin,end,L['mip_size']):
    pixels_begin,pixels_end=struct.unpack('<QQ',read(item+L['mip_pixels'],16))
    if not 0<=pixels_end-pixels_begin<=1024**3:raise ValueError('invalid mip pixels')
    g['cpu_mip_bytes']+=pixels_end-pixels_begin
  def rotated(address,owner):
   if not address:return
   surface(u64(address),owner)
   for nodevalue in tree(address+L['rot_map']):surface(u64(nodevalue+8),owner)
  def sprite(address,owner=None):
   if not address or address in seen_sprites:return
   raw=read(address,L['sprite_size']);seen_sprites.add(address);owner=owner or name(raw)
   for field in ['images','hd_images']:
    for item_address in vector(raw,L[field]):surface(item_address,owner)
   for field in ['rotated','hd_rotated']:
    for item_address in vector(raw,L[field]):rotated(item_address,owner)
   surface(ptr(raw,L['atlas']),owner);sprite(ptr(raw,L['hd_atlas']),owner)
   head=address+L['team_list'];node=u64(head);visited=set()
   while node and node!=head:
    if node in visited or len(visited)>10000:raise ValueError('invalid list')
    visited.add(node);surface(u64(node+16+L['team_surface']),owner);node=u64(node)
  registry_before=read(registry_address,48)
  for nodevalue in tree(registry_address):sprite(u64(nodevalue))
  if registry_before!=read(registry_address,48):raise ValueError('registry changed during capture')
 return {'groups':dict(groups),'registered_and_nested_sprites':len(seen_sprites),'unique_surfaces':len(seen_surfaces),'cpu_pixel_bytes':sum(g['cpu_pixel_bytes'] for g in groups.values()),'gpu_texture_bytes':sum(g['gpu_texture_bytes'] for g in groups.values()),'cpu_mip_bytes':sum(g['cpu_mip_bytes'] for g in groups.values()),'cpu_upload_bytes':sum(g['cpu_upload_bytes'] for g in groups.values())}

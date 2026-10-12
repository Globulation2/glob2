import anyio, base64, json, sys
from pathlib import Path
from mcp import Client
from mcp.types import Implementation

async def run(mode):
    async with Client('http://127.0.0.1:27202/mcp', mode=mode,
                      client_info=Implementation(name='glob2-validation-'+mode, version='1')) as client:
        print('protocol',client.protocol_version,flush=True)
        listing=await client.list_tools()
        assert len(listing.tools)==12
        status=await client.call_tool('ui_status')
        assert not status.is_error
        acquired=await client.call_tool('ui_acquire_control')
        assert not acquired.is_error, acquired
        token=acquired.structured_content['token']
        viewport=status.structured_content['viewport_id']
        held=await client.call_tool('ui_key',{'token':token,'key':'Left Ctrl','action':'down'})
        assert not held.is_error
        pressed=await client.call_tool('ui_key',{'token':token,'key':'A','modifiers':['Left Ctrl']})
        assert not pressed.is_error
        released=await client.call_tool('ui_release_inputs',{'token':token})
        assert not released.is_error
        image=await client.call_tool('ui_screenshot')
        assert not image.is_error, image
        png=next(block for block in image.content if block.type=='image')
        assert png.mime_type=='image/png'
        Path('artifacts/mcp/sdk-'+mode+'.png').write_bytes(base64.b64decode(png.data))
        bad=await client.call_tool('ui_move',{'token':token,'x':5,'y':5,'viewport_id':viewport+1000})
        assert bad.is_error
        print('status',json.dumps(status.structured_content),flush=True)
        print('capture',json.dumps(image.structured_content),flush=True)
        assert not (await client.call_tool('ui_release_control',{'token':token})).is_error
        print('PASS',mode,flush=True)

async def main():
    for mode in ('auto','legacy'):
        await run(mode)
anyio.run(main)

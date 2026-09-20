import os

def create_dmg( target, source, env) :
    # target and source are lists of scons Nodes, so each has to be named
    # explicitly: formatting the list itself yields "[Glob2.app]" and the copy
    # silently finds nothing to copy.
    image = str(target[0])
    volume = os.path.basename(image).replace('.dmg', '')
    os.system( "rm -f %s"%image )
    os.system( "rm -rf DMG" )
    os.system( "mkdir DMG" )
    for item in source :
        os.system( "cp -r %s DMG"%str(item) )
    os.system( "hdiutil create -srcfolder DMG -volname %s %s"%(volume, image) )
    os.system( "rm -rf DMG" )

def create_dmg_message( target, source, env):
    return "Creating DMG package"

def generate(env) :
    """Add Builders and construction variables for qt to an Environment."""
    print("Loading dmg tool...")
    env.Append( BUILDERS={'Dmg' : 
            env.Builder( action=env.Action(create_dmg, create_dmg_message ))
        } )

def exists(env) :
    return True


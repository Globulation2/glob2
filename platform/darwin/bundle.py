import SCons.Util, os
import sys
sys.path.append( os.path.dirname(__file__) )
from addDependentLibsToBundle import addDependentLibsToBundle

def run(command) :
	print "\033[32m:: ", command, "\033[0m"
	return os.system(command)
def norun(command) :
	print "\033[31mXX ", command, "\033[0m"



def createBundle(target, source, env) :
	bundleDir = env['BUNDLE_NAME']+'.app'
	run("rm -rf "+bundleDir )
	run("mkdir -p %s/Contents/Resources" % bundleDir )
	run("mkdir -p %s/Contents/Frameworks" % bundleDir )
	run("mkdir -p %s/Contents/MacOS" % bundleDir )
	# add binaries
	for bin in env.Flatten( env['BUNDLE_BINARIES'] ) :
		run('cp %s %s/Contents/MacOS/' % (str(bin), bundleDir) )
	# add resources
	for resdir in env['BUNDLE_RESOURCEDIRS'] :
		# TODO act sensitive to resdir being a scons target. now assuming a string
		run('cp -r %s %s/Contents/Resources/' % (str(resdir), bundleDir) )
	# write Info.plist -- TODO actually write it not copy it
	plistFile = env['BUNDLE_PLIST']
	run('cp %s %s/Contents/Info.plist' % (plistFile, bundleDir) )
	# add icon -- TODO generate .icns file from png or svg
	iconFile = env['BUNDLE_ICON']
	run('cp %s %s/Contents/Resources' % (iconFile, bundleDir) )
	# add dependent libraries, fixing all absolute paths
	addDependentLibsToBundle( bundleDir )
	

def createBundleMessage(target, source, env) :
	out ="Running Bundle builder\n"
	for a in target : out+= "Target:"+ str(a) + "\n"
	for a in source : out+= "Source:"+ str(a) + "\n"
	return out

def bundleEmitter(target, source, env):
	target = env.Dir(env['BUNDLE_NAME']+".app")
	source = env['BUNDLE_BINARIES']
	return target, source

def generate(env) :
	print "Loading Bundle tool"
	Builder = SCons.Builder.Builder
	Action = SCons.Action.Action
	bundleBuilder = Builder(
		action = Action( createBundle, createBundleMessage ),
		emitter = bundleEmitter,
	)
	env['BUNDLE_RESOURCEDIRS'] = []
	env['BUNDLE_PLUGINS'] = []
	env.Append( BUILDERS={'Bundle' : bundleBuilder } )

def exists(env) :
	return True

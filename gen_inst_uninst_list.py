import sys, os, glob

#
# FUNCTION TAKEN FROM http://nsis.sourceforge.net/Talk:Uninstall_only_installed_files
#
def open_file_for_writting(filename):
    "return a handle to the file to write to"
    try:
        h = file(filename, "w")
    except:
        print "Problem opening file %s for writting"%filename
        print __doc__
        sys.exit(1)
    return h

#
# OPEN FILES
#
install_list = open_file_for_writting("windows/install_list.nsh")
uninstall_list = open_file_for_writting("windows/uninstall_list.nsh")

#
# PREPARE ARRAY FOR UNINSTALL LIST
#
folder_list = []
file_list = []

#
# INSTALL LIST
#
for path,dirs,files in list(os.walk('data')) + list(os.walk('maps')) + list(os.walk('campaigns')) + list(os.walk('scripts')):
	print >> install_list, "SetOutPath $INSTDIR\\" + path
	folder_list.append(path)
	for f in files:
		if (f != "SConscript" and f != "SConstruct"):
			print >> install_list, "File ..\\"+path+"\\"+f
			file_list.append(path+"\\"+f)
print >> install_list, "SetOutPath $INSTDIR"
for file in list(os.listdir('./')):
	 if (file.find(".dll") != -1):
		print >> install_list, "File ..\\"+file
		file_list.append(file)

#
# UNINSTALL LIST
#
file_list.reverse()
for file in file_list:
	print >> uninstall_list, "Delete $INSTDIR\\"+file
folder_list.reverse()
for folder in folder_list:
	print >> uninstall_list, "RMDir $INSTDIR\\"+folder

#
# CLOSE FILES
#
install_list.close()
uninstall_list.close()
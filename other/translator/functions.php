<?php
//TextFileDirectory
define("TFD", "./glob2/data/");
//define("TFD", "./textfiles/");
//define("TFD", "./hgtexfiles/");
//appends the line "[$key]\n" to ./textfiles/texts.keys.txt
//returns an error if the key already exists
function insertNewKey($key) {
	$r = fopen (TFD."texts.keys.txt", "rb");
	$retVal="";
	while (!feof($r)) {
		$line = fgets($r, 4096);
		if (strpos($line,"[$key]") !== false ) {
			$retVal="$key is already defined$line$key";
			break;
		}
	}
	fclose ($r);
	if($retVal!="") {
		return $retVal;
	}
	$a = fopen (TFD."texts.keys.txt", "a");
	fwrite($a,"[$key]\n");
	fclose ($a);
	return "success";
}
//if [$key] is found in ./textfiles/texts.$language.txt the value is updated
//else [$key]\n$value\n id appended
function updateTranslation($language,$key,$value) {
    $updated=false;
    $filename=TFD."texts.$language.txt";
    if(!file_exists($filename)) {
        return "unknown language";
    }
    $tmpfilename=tempnam("./","tmp");
    $r = fopen ($filename, "rb");
    $w = fopen ($tmpfilename, "wb");
    while (!feof($r)) {
        $line = fgets($r, 4096);
        fwrite($w,$line);
        if (strpos($line,"[$key]") !== false ) {
            $updated=true;
            $line = fgets($r, 4096);
            fwrite($w,"$value\n");
            echo "$value\n";
        }
    }
    //key not found
    if(!$updated) {
            fwrite($w,"[$key]\n$value\n");
    }
    fclose ($w);
    fclose ($r);
    if(!unlink($filename)) {
        return "delete failed";
    }
    if(!rename($tmpfilename,$filename)) {
        return "rename failed";
    }
    chmod($filename, 0666);
    return "success";
}
/*function getlanguagefiles () {
	$translations = getLanguages();
//get all the keys
	$keys = array();
	$handle = fopen ("./textfiles/texts.keys.txt", "r");
	while (!feof($handle)) {
		$line = fgets($handle, 4096);
		$tmp1=explode("[", $line);
		$tmp2=explode("]",$tmp1[1]);
		$keys[$tmp2[0]]=array();
	}
	fclose ($handle);
	//set default values
	foreach($translations as $k => $v) {
		foreach($keys as $k2 => $v2) {
			$keys[$k2][$v] = "[]";
		}
	}
	foreach($translations as $k => $v) {
		$handle = fopen ("./textfiles/texts.$v.txt", "r");
		while (!feof($handle)) {
			$line = fgets($handle, 4096);
			$tmp1=explode("[", $line);
			$tmp2=explode("]",$tmp1[1]);
			$tmpkey=$tmp2[0];
			$tmpvalue = fgets($handle, 4096);
			$keys[trim($tmpkey)][$v] = trim($tmpvalue);
		}
		fclose ($handle);
	}
	return array('translations' => $keys,'languages' => $translations);
}*/
function getTranslations($translations, $first, $count) {
	if(!isset($translations)) {
		$translations=array();
	}
	$keys = array();
	$handle = fopen (TFD."texts.keys.txt", "r");
	$results=0;
	while (!feof($handle)) {
		$line = fgets($handle, 4096);
		$results++;
		if($results >= $first and $results < $first+$count) {
			$tmp1=explode("[", $line);
			$tmp2=explode("]",$tmp1[1]);
			$keys[$tmp2[0]]=array();
		}
	}
	fclose ($handle);
	//set default values
	foreach($translations as $k => $v) {
		foreach($keys as $k2 => $v2) {
			$keys[$k2][$v] = "[]";
		}
	}
	foreach($translations as $k => $v) {
		$filename=TFD."texts.$v.txt";
		if(file_exists($filename)) {
			$handle = fopen ($filename, "r");
		} else {
			continue;
		}
		while (!feof($handle)) {
			$line = fgets($handle, 4096);
			$tmp1=explode("[", $line);
			$tmp2=explode("]",$tmp1[1]);
			$tmpkey=$tmp2[0];
			$tmpvalue = fgets($handle, 4096);
			if(isset($keys[trim($tmpkey)][$v])) {
				$keys[trim($tmpkey)][$v] = trim($tmpvalue);
			}
		}
		fclose ($handle);
	}
	return array('translations' => $keys,'languages' => $translations, 'results' => $results);
}
function getLanguages() {
	$translations = array();
	$d = dir(TFD);
	while($entry = $d->read()) {
		if ($entry != "." 
					&& $entry != ".." 
					&& $entry != "texts.keys.txt"
					&& $entry != "texts.list.txt"
					&& ! is_dir(TFD.$entry)) {
			if(substr($entry,-4) == ".txt") {
				$tmp1=substr($entry,0,strlen($entry)-4);
				$tmp2=explode(".",$tmp1);
				$translations[]=$tmp2[1];
			}
		}
	}
	$d->close();
	return $translations;
}
?>
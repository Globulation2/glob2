<?php
include "functions.php";
//print_r($_GET);
//$languages=array();
if(!isset($_GET['languages'])) {
	$languages=array();
} else {
	$languages=split(',',$_GET['languages']);
}
$first=(isset($_POST['start'])?$_POST['start']:0);
$count=(isset($_POST['limit'])?$_POST['limit']:25);
$content=getTranslations($languages, $first, $count);
$langKey=$content["translations"];
$results=$content["results"];
foreach($langKey as $k => $v) {
	$langKey[$k]['key']=$k;
}
echo "{'results': $results , 'rows': [\n";
$i=0;
foreach($langKey as $key => $translations)
{
	echo "  ".($i++>0?",":"").json_encode($translations);
}
echo "]}";
?>
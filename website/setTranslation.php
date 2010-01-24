<?php
//check if we got a correct call
if(isset($_GET['submitted']))
{
	include "functions.php";
	$value=stripslashes($_GET['newvalue']);
	$key=stripslashes($_GET['key']);
	$language=stripslashes($_GET['language']);
	if($language=='key') {
		echo insertNewKey($value);
	} else {
		echo updateTranslation($language,$key,$value);
	}
}
else
{
	echo 'wrong call';
}
?>
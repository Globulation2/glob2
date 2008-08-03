<?php
if(isset($_GET['submitted'])) {
	$comment=$_GET['comment'];
	$comment = escapeshellcmd($comment);

	// here we don't care if $e has spaces
	system("/home/leo/test.leowandersleb.de/website/translation/commit.sh $comment");
	//shell_exec('/home/leo/test.leowandersleb.de/website/translation/commit.sh '.$comment);
	echo "success";
}
else
{
	echo 'wrong call';
}
?>
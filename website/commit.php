<?php
$to="Leo.Wandersleb@gmx.de";
$subject="commit those changes, please!";

if(isset($_GET['submitted'])) {
	$comment=$_GET['comment'];
	$comment = escapeshellcmd($comment);
	//as an intermediate solution, the stuff does not get committed but sent to leo.
	mail($to,$subject,$comment) or die("failed to send");

//	//commit using the provided comment
//	system("/home/leo/test.leowandersleb.de/website/translation/commit.sh $comment");
//	//shell_exec('/home/leo/test.leowandersleb.de/website/translation/commit.sh '.$comment);
	echo "success";
}
else
{
	echo 'wrong call';
}
?>
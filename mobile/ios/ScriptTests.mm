// SPDX-License-Identifier: GPL-3.0-or-later
// Separate developer harness; never reads the normal game's Documents container.
#import <UIKit/UIKit.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

extern int glob2ScriptTestMain(int, char**);

@interface Glob2ScriptTestsDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow* window;
@end
@implementation Glob2ScriptTestsDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
 (void)application; (void)options;
 self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
 UIViewController* controller=[UIViewController new];
 controller.view.backgroundColor=UIColor.systemBackgroundColor;
 UILabel* status=[[UILabel alloc] initWithFrame:CGRectMake(24,100,340,200)];
 status.numberOfLines=0; status.text=@"Running production JavaScript tests…";
 [controller.view addSubview:status]; self.window.rootViewController=controller;
 [self.window makeKeyAndVisible];
 dispatch_async(dispatch_get_main_queue(),^{
  NSString* documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
  NSString* evidence=[documents stringByAppendingPathComponent:@"ScriptingEvidence"];
  NSFileManager* files=NSFileManager.defaultManager;
  [files removeItemAtPath:evidence error:nil];
  NSString* runId=@"manual";
  for(NSString* argument in NSProcessInfo.processInfo.arguments)
   if([argument hasPrefix:@"--glob2-script-run="]) runId=[argument substringFromIndex:19];
  [files createDirectoryAtPath:evidence withIntermediateDirectories:YES attributes:nil error:nil];
  NSString* assets=NSBundle.mainBundle.resourcePath;
  setenv("GLOB2_TEST_SOURCE_ROOT",assets.fileSystemRepresentation,1);
  setenv("GLOB2_ASSET_DIR",assets.fileSystemRepresentation,1);
  setenv("GLOB2_USER_DATA_DIR",[evidence stringByAppendingPathComponent:@"profile"].fileSystemRepresentation,1);
  setenv("GLOB2_TEST_ARTIFACTS_ROOT",[evidence stringByAppendingPathComponent:@"corpus"].fileSystemRepresentation,1);
  chdir(assets.fileSystemRepresentation);
  freopen([evidence stringByAppendingPathComponent:@"tests.log"].fileSystemRepresentation,"w",stdout);
  freopen([evidence stringByAppendingPathComponent:@"errors.log"].fileSystemRepresentation,"w",stderr);
  std::string report="--out="+std::string([evidence stringByAppendingPathComponent:@"tests.xml"].fileSystemRepresentation);
  char name[]="glob2-script-tests",filter[]="--test-suite=JavaScript*",reporter[]="--reporters=junit";
  char* arguments[]={name,filter,reporter,report.data()};
  int result=glob2ScriptTestMain(4,arguments);
  fflush(stdout); fflush(stderr);
  NSMutableDictionary* summary=[@{@"runId":runId,@"exitCode":@(result),@"platform":UIDevice.currentDevice.systemVersion,
    @"model":UIDevice.currentDevice.model,@"bundleId":NSBundle.mainBundle.bundleIdentifier} mutableCopy];
  NSData* metadata=[NSData dataWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"scripting-build" ofType:@"json"]];
  if(metadata) summary[@"build"]=[NSJSONSerialization JSONObjectWithData:metadata options:0 error:nil];
  [[NSJSONSerialization dataWithJSONObject:summary options:NSJSONWritingPrettyPrinted error:nil]
    writeToFile:[evidence stringByAppendingPathComponent:@"result.json"] atomically:YES];
  status.text=[NSString stringWithFormat:@"JavaScript tests %@ (exit %d).\nExport ScriptingEvidence from this test app’s Documents folder.",result==0?@"passed":@"failed",result];
 });
 return YES;
}
@end
int main(int argc,char** argv)
{
 @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(Glob2ScriptTestsDelegate.class)); }
}

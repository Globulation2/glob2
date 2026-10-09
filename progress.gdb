set pagination off
set print elements 2
break src/render/HighResolutionIntegrationHarness.cpp:46 if !complete
run
bt 3
python
import time
start=time.monotonic()
done=False
while time.monotonic()-start<120:
    done=bool(gdb.parse_and_eval('GAGCore::Toolkit::pollAssets(4)'))
    if done: break
    time.sleep(.001)
print('DIAGNOSTIC extra_seconds=',time.monotonic()-start,'completed=',done)
end
print GAGCore::pendingHighResolution
quit

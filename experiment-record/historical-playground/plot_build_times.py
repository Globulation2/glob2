#!/usr/bin/env python3
"""Plot raw successful invocations plus logged work; never normalize wall by jobs."""
from pathlib import Path
import csv,json,re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
ROOT=Path(__file__).resolve().parent
config=json.loads((ROOT/'experiment.json').read_text())
audit_path=ROOT.parent/'engine-investigations/build-stages/build-audit.json'
audit={row['sample_date']:row for row in json.loads(audit_path.read_text())['rows']}
colors={2:'#c07a33',8:'#7179aa',32:'#126a6b'}
records=[]
fig,(ax,work)=plt.subplots(2,1,figsize=(13,9),layout='constrained',sharex=True)
fig.get_layout_engine().set(rect=(0,.055,1,.955))
for index,snap in enumerate(config['snapshots']):
 path=ROOT/'snapshots'/(snap['sample_date']+'-'+snap['commit'][:9])/'build.json'
 if not path.exists():continue
 original=json.loads(path.read_text());command=original['command']
 jobs=next(int(arg[2:]) for arg in command if re.fullmatch(r'-j\d+',arg))
 seconds=original['build_wall_seconds'];mask=command[command.index('-c')+1] if command[0]=='taskset' else 'not recorded in command'
 observed=audit[snap['sample_date']]
 record=dict(sample_date=snap['sample_date'],commit=snap['commit'],successful_invocation_seconds=seconds,
     ideal_32_job_seconds=seconds*jobs/32,ideal_32_job_seconds_is_measurement=False,normalization_valid=False,
     jobs=jobs,cpu_affinity=mask,command=' '.join(command),compile_command_count=observed['compile_command_count'],
     link_command_count=observed['link_command_count'],compiler_cpu_seconds=None,cache_hit_count=None,
     invocation_class=observed['invocation_class'],clean_build_established=False,asset_export=observed['asset_export'],
     attempt_count=observed['attempt_count'],all_attempts=observed['all_attempts'],
     asset_source_file_count=observed['asset_source_file_count'],asset_source_bytes=observed['asset_source_bytes'],
     png_source_count=observed['png_source_count'],png_source_bytes=observed['png_source_bytes'],
     png_pixel_count=observed['png_pixel_count'],png_depth_color_counts=observed['png_depth_color_counts'],
     png_area_counts=observed['png_area_counts'],packaged_asset_audits=observed['packaged_asset_audits'],
     current_asset_cache_entries=observed['current_asset_cache_entries'],current_asset_cache_bytes=observed['current_asset_cache_bytes'],
     asset_inventory_policy=observed['inventory_policy'],build_manifest_sha256=observed['build_manifest_sha256'])
 records.append(record)
 noop=observed['compile_command_count']==0
 bars=ax.bar(index,seconds/60,color=colors[jobs] if not noop else '#a9afb5',width=.66,
             hatch='///' if noop else None,edgecolor='#64717c' if noop else colors[jobs])
 ax.bar_label(bars,labels=[f'{seconds/60:.1f}m'+ ('\nNO compiles' if noop else '')],padding=4,fontsize=8)
 work.bar(index,observed['compile_command_count'],color=colors[jobs],width=.66)
 work.text(index,observed['compile_command_count']+15,str(observed['compile_command_count']),ha='center',fontsize=9)
 if observed['asset_export']:work.text(index,observed['compile_command_count']*.5,'asset\nexport',ha='center',va='center',color='white',fontsize=8)
work.set_xticks(range(len(config['snapshots'])),[snap['sample_date'][5:] for snap in config['snapshots']],rotation=45,ha='right')
ax.set_ylabel('Observed invocation wall time (minutes)')
ax.set_ylim(0,max(row['successful_invocation_seconds'] for row in records)/60*1.2)
ax.set_title('Final successful SCons invocations — mixed cache, resume and job counts',loc='left',fontsize=15,fontweight='bold')
ax.legend(handles=[Patch(color=color,label=f'{jobs} requested jobs') for jobs,color in colors.items()]+[Patch(facecolor='#a9afb5',hatch='///',label='No logged compiles; targets up to date')],loc='upper left',frameon=False,fontsize=9)
work.set_title('Logged native compiler commands in that invocation (may include ccache hits)',loc='left',fontsize=13,fontweight='bold')
work.set_ylabel('Printed compiler command count');work.set_ylim(0,max(row['compile_command_count'] for row in records)*1.17)
for axes in (ax,work):
 axes.spines[['top','right']].set_visible(False);axes.grid(axis='y',alpha=.18);axes.set_axisbelow(True)
fig.text(.04,.013,'Not a clean-build comparison. Sep 2/5 are resumed/no-op checks. Compiler CPU/cache hit rates are unavailable.\nAsset export is one serial action; elapsed × jobs ÷ 32 is not a valid normalization and is not plotted.',fontsize=9,color='#52616b')
fig.savefig(ROOT/'build-times.png',dpi=160,bbox_inches='tight');fig.savefig(ROOT/'build-times.svg',bbox_inches='tight')
with (ROOT/'build-times.csv').open('w',newline='') as stream:
 writer=csv.DictWriter(stream,fieldnames=list(records[0]));writer.writeheader()
 writer.writerows({key:json.dumps(value,separators=(',',':')) if isinstance(value,(list,dict)) else value for key,value in row.items()} for row in records)
(ROOT/'build-times.json').write_text(json.dumps(records,indent=2)+'\n')
print('Updated',len(records),'raw invocation rows; no job-scaling chart; compiler CPU stays null')

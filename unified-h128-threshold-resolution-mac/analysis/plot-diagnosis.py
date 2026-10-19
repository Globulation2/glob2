from pathlib import Path
import json,csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle,Patch
R=Path(__file__).resolve().parent;d=json.loads((R/'DIAGNOSIS.json').read_text());bands=[b for b in d['populations']['holdout']['score_bands'] if b['raw_rows']];plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'savefig.dpi':180})
fig,ax=plt.subplots(figsize=(11,5))
for i,b in enumerate(bands):
 lo,hi=b['prediction_range_units'];ax.add_patch(Rectangle((i-.19,lo),.38,hi-lo,color='#888888',alpha=.3));actual=b['weighted_actual_mean'];ax.scatter(i,actual,color='#0072B2' if actual>=0 else '#D55E00',s=42,zorder=3);ax.annotate('n='+str(b['raw_rows']),(i,actual),textcoords='offset points',xytext=(0,7),ha='center',fontsize=8)
ax.set_xticks(range(len(bands)));ax.set_xticklabels([str(b['prediction_range_units'][0])+'–'+str(b['prediction_range_units'][1]) for b in bands]);ax.set_yscale('symlog',linthresh=2);ax.set_ylim(-140,260);ax.set_yticks([-128,-32,-8,-2,0,2,8,32,128]);ax.set_yticklabels(['−128','−32','−8','−2','0','2','8','32','128']);ax.axhline(0,color='black',lw=.7);ax.grid(axis='y',alpha=.16);ax.set_xlabel('Fixed gen003 predicted score band (direction-cost units)');ax.set_ylabel('Heldout weighted mean actual signed benefit');ax.set_title('Useful lower bands; sparse extreme tail reverses in this sample',fontsize=13);ax.legend(handles=[Patch(color='#888888',alpha=.3,label='Prediction interval (not a confidence interval)'),plt.Line2D([0],[0],marker='o',ls='',color='#0072B2',label='Observed signed mean')],loc='upper left',fontsize=8);fig.text(.5,.01,'Same sealed 27,669 heldout episodes/120 maps. Counts are raw; means use existing hierarchy weights. Top tail is map-concentrated.',ha='center',fontsize=8);fig.tight_layout(rect=[0,.04,1,1])
for e in ['png','pdf']:fig.savefig(R/('scoreband-calibration.'+e))
plt.close(fig)
rows=list(csv.DictReader((R/'family-threshold-table.csv').open()));fig,ax=plt.subplots(figsize=(10,5));colors={'materials':'#0072B2','clear':'#D55E00','guard':'#009E73','forbidden':'#CC79A7'}
for family,color in colors.items():
 p=[x for x in rows if x['family']==family and x['conditional_weighted_selected_actual_mean']];x=[int(z['threshold_units']) for z in p];y=[float(z['conditional_weighted_selected_actual_mean']) for z in p];ax.plot(x,y,'o-',color=color,label=family)
 for z in p:
  if z['threshold_units']=='128':ax.annotate('n='+z['raw_selected'],(128,float(z['conditional_weighted_selected_actual_mean'])),xytext=(-35,8),textcoords='offset points',fontsize=9,color=color)
ax.plot([1,128],[1,128],'--',color='#888888',lw=1,label='Cutoff value');ax.axhline(0,color='black',lw=.7);ax.set_xscale('log',base=2);ax.set_xticks([1,4,8,16,32,128]);ax.set_xticklabels(['1','4','8','16','32','128']);ax.set_xlabel('Runtime score cutoff (same fixed model)');ax.set_ylabel('Actual signed benefit among selected episodes');ax.set_title('Conditional family calibration differs; extreme estimates are sparse',fontsize=12);ax.grid(alpha=.15);ax.legend(ncol=3,loc='lower left',fontsize=9);fig.text(.5,.01,'Family hierarchy weights recomputed within subgroup. Clear tail: 13 rows/4 maps; negative harm on 2 maps. Markets unmeasured.',ha='center',fontsize=8);fig.tight_layout(rect=[0,.04,1,1])
for e in ['png','pdf']:fig.savefig(R/('family-selected-benefit.'+e))

import math,statistics
# One-sided 95% Student-t quantiles for paired log-ratios, df 1 through 20.
T=[None,6.313752,2.919986,2.353363,2.131847,2.015048,1.943180,1.894579,1.859548,1.833113,1.812461,1.795885,1.782288,1.770933,1.761310,1.753050,1.745884,1.739607,1.734064,1.729133,1.724718]
def confidence(ratios):
 logs=[math.log(x) for x in ratios];mean=statistics.mean(logs)
 upper=mean+T[len(logs)-1]*statistics.stdev(logs)/math.sqrt(len(logs)) if len(logs)>1 else float('inf')
 return {'change_percent':100*math.expm1(mean),'upper95_percent':100*math.expm1(upper),'pairs':len(logs),'method':'one-sided paired log-ratio Student-t'}

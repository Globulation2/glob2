import json,glob,statistics as st,sys
for g in sys.argv[1:]:
  f=[];cv=[];fert=[];cat=[]
  for p in glob.glob(f'fair/{g}-*/result.json'):
    r=json.load(open(p))
    if r.get('status')!='completed' or 'quality' not in r: continue
    q=r['quality']; f.append(q['fairness'])
    c=[x['catchment_tiles'] for x in q['colonies']]; cv.append(st.pstdev(c)/st.mean(c)); cat.append(st.mean(c))
    fe=[x['mean_fertility'] for x in q['colonies']]; fert.append(max(fe)/max(1,st.mean(fe)))
  f.sort(); print(f'{g:9}',len(f),'fairness median',round(st.median(f),3),'p10',round(f[len(f)//10],3),'min',round(f[0],3),'| catchment mean',round(st.mean(cat)),'CV',round(st.mean(cv),3),'| max/mean fert',round(st.median(fert),2))

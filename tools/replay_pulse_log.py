# Python port of pulleyPulse()/pulleyUpdate() (same integer logic) replayed on the raw log
import sys, math
ev=[(int(t),int(v)) for l in open(sys.argv[1]) if l.startswith('E,') for _,t,v in [l.strip().split(',')]]
# debounce 15 ms on all edges, count on closing edge (as firmware)
pulses=[]; last=-10**9
for t,v in ev:
    if t-last>=15000:
        last=t
        if v==0: pulses.append(t//1000)
GAPMIN,GAPPCT,LOCK,NDRV,K=400,160,70,2,2.8
circ=0.11*math.pi
S=dict(lastPulse=0,lastStroke=0,driveStart=0,driveCount=-1,lastPeriod=0,periods=[],drives=[],iv=[],strokes=0)
def pulse(t):
    iv=t-S['lastPulse']; S['lastPulse']=t
    med=sorted(S['iv'])[len(S['iv'])//2] if len(S['iv'])>=4 else 300
    since=t-S['lastStroke']; lock=S['lastPeriod']*LOCK//100 if S['lastPeriod'] else 1000
    gap= iv>=GAPMIN and iv*100>med*GAPPCT
    if gap and since>=lock:
        if S['lastStroke'] and since<6000:
            S['lastPeriod']=since; S['periods']=(S['periods']+[since])[-3:]
        else: S['lastPeriod']=0
        S['lastStroke']=t; S['strokes']+=1; S['driveStart']=t; S['driveCount']=0
    elif gap: S['driveCount']=-1
    else:
        S['iv']=(S['iv']+[iv])[-8:]
        if S['driveCount']>=0:
            S['driveCount']+=1
            if S['driveCount']==NDRV:
                S['drives']=(S['drives']+[t-S['driveStart']])[-3:]; S['driveCount']=-1
def update(now):
    per=sum(S['periods'])//len(S['periods']) if S['periods'] else 0
    drv=sum(S['drives'])//len(S['drives']) if S['drives'] else 0
    since=now-S['lastStroke']; to=max(3000,2*per) if per else 3000
    if S['lastStroke'] and since<to:
        return (60000/per if per else 0, K*circ*NDRV*1000/drv if drv else 0)
    S['periods']=[];S['drives']=[];S['lastPeriod']=0
    return (0,0)
t0=pulses[0]; i=0; meters=0.0; out=[]
for now in range(t0, pulses[-1]+5000, 1000):
    while i<len(pulses) and pulses[i]<=now: pulse(pulses[i]); i+=1
    spm,v=update(now); meters+=v
    out.append((now-t0,spm,v,meters,S['strokes']))
for s_,spm,v,m,st in out[::3]:
    pace=f"{int(500/v)//60}:{int(500/v)%60:02d}" if v>0.05 else "--:--"
    print(f"t={s_/1000:5.0f}s  spm {spm:4.1f}  v {v:4.2f} m/s  pace {pace}  dist {m:6.0f} m  strokes {st}")
print("total strokes",S['strokes'])

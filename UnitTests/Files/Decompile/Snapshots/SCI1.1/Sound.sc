;;; Sierra Script 1.0 - (do not remove this comment)
(script# 989)
(include sci.sh)
(use Main)
(use System)


(class Sound of Obj
	(properties
		nodePtr 0
		handle 0
		flags 0
		number 0
		vol 127
		priority 0
		loop 1
		signal 0
		prevSignal 0
		dataInc 0
		min 0
		sec 0
		frame 0
		client 0
		owner 0
	)
	
	(method (new param1)
		((super new:) owner: (if argc param1 else 0) yourself:)
	)
	
	(method (init)
		(= prevSignal (= signal 0))
		(gSounds add: self)
		(DoSound 6 self)
	)
	
	(method (dispose)
		(gSounds delete: self)
		(if nodePtr (DoSound 7 self) (= nodePtr 0))
		(super dispose:)
	)
	
	(method (play theClient &tmp theParamTotal)
		(= theParamTotal argc)
		(if (and argc (IsObject [theClient (- argc 1)]))
			(= theParamTotal (- argc 1))
			(= client [theClient theParamTotal])
		else
			(= client 0)
		)
		(if (not (gSounds contains: self)) (self init:))
		(if (not loop) (= loop 1))
		(if theParamTotal (= vol theClient) else (= vol 127))
		(DoSound 8 self 0)
	)
	
	(method (stop)
		(if handle (DoSound 17 self) (DoSound 9 self))
	)
	
	(method (pause param1)
		(if (not argc) (= param1 1))
		(DoSound
			10
			(if (self isMemberOf: Sound) self else 0)
			param1
		)
	)
	
	(method (hold param1)
		(if (not argc) (= param1 1))
		(DoSound 12 self param1)
	)
	
	(method (release)
		(DoSound 12 self 0)
	)
	
	(method (fade theClient param2 param3 param4 &tmp theParamTotal)
		(= theParamTotal argc)
		(if (and argc (IsObject [theClient (- argc 1)]))
			(= theParamTotal (- argc 1))
			(= client [theClient theParamTotal])
		)
		(if theParamTotal
			(DoSound 11 self theClient param2 param3 param4)
		else
			(DoSound 11 self 0 25 10 1)
		)
	)
	
	(method (mute param1 param2 &tmp temp0)
		(if (not argc) (= param1 1))
		(if (< argc 2)
			(= temp0 1)
			(while (< temp0 17)
				(DoSound 18 self temp0 78 param1)
				(++ temp0)
			)
		else
			(DoSound 18 self param2 78 param1)
		)
	)
	
	(method (setVol param1)
		(DoSound 14 self param1)
	)
	
	(method (setPri param1)
		(DoSound 15 self param1)
	)
	
	(method (setLoop param1)
		(DoSound 16 self param1)
	)
	
	(method (snd2 param1 param2 param3 param4)
		(if (and (<= 1 param1) (<= param1 15))
			(if (< param2 128)
				(DoSound 18 self param1 176 param2 param3)
			else
				(DoSound 18 self param1 param2 param3 param4)
			)
		)
	)
	
	(method (check)
		(if handle (DoSound 17 self))
		(if signal
			(= prevSignal signal)
			(= signal 0)
			(if (IsObject client) (client cue: self))
		)
	)
	
	(method (clean param1)
		(if (or (not owner) (== owner param1))
			(self dispose:)
		)
	)
	
	(method (playBed theClient &tmp theParamTotal)
		(= theParamTotal argc)
		(if (and argc (IsObject [theClient (- argc 1)]))
			(= theParamTotal (- argc 1))
			(= client [theClient theParamTotal])
		else
			(= client 0)
		)
		(self init:)
		(if (not loop) (= loop 1))
		(if theParamTotal (= vol theClient) else (= vol 127))
		(DoSound 8 self 1)
	)
	
	(method (changeState)
		(DoSound 20 self)
	)
)

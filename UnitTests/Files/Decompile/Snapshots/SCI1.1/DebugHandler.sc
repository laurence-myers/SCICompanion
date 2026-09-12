;;; Sierra Script 1.0 - (do not remove this comment)
(script# 10)
(include sci.sh)
(use Main)
(use Controls)
(use Print)
(use DialogControls)
(use PolygonEdit)
(use DialogEdit)
(use FeatureWriter)
(use Feature)
(use SysWindow)
(use InventoryItem)
(use User)
(use Actor)
(use System)

(public
	debugHandler 0
	dInvD 1
)

(local
	[local0 27]
	newDButton
	[local1 2]
)
(procedure (localproc_002e)
	(if (IsOneOf (gRoom style?) 11 12 13 14)
		(gRoom drawPic: (gRoom picture?) 100 style: 100)
	)
)

(instance debugHandler of Feature
	(properties)
	
	(method (init)
		(super init:)
		(gOldMH addToFront: self)
		(gOldKH addToFront: self)
	)
	
	(method (dispose)
		(gOldMH delete: self)
		(gOldKH delete: self)
		(super dispose:)
		(DisposeScript 10)
	)
	
	(method (handleEvent param1 &tmp [temp0 160] gRoomObstacles newEvent gCastFirst theGFont temp164 temp165 temp166 temp167 temp168 temp169 temp170 temp171 temp172 userAlterEgo temp174 temp175 temp176 temp177 temp178)
		(switch (param1 type?)
			(4
				(param1 claimed: 1)
				(switch (param1 message?)
					(7680
						(= gCastFirst (gCast first:))
						(while gCastFirst
							(= temp164 (NodeValue gCastFirst))
							(Format
								@temp0
								10
								1
								((temp164 -super-?) name?)
								(temp164 view?)
								(temp164 loop?)
								(temp164 cel?)
								(temp164 x?)
								(temp164 y?)
								(temp164 z?)
								(temp164 heading?)
								(temp164 priority?)
								(temp164 signal?)
								(if (temp164 isKindOf: Actor)
									(temp164 illegalBits?)
								else
									-1
								)
							)
							(if
								(not
									(Print
										addText:
											@temp0
											(CelWide
												(temp164 view?)
												(temp164 loop?)
												(temp164 cel?)
											)
											0
										window: SysWindow
										addTitle: (temp164 name?)
										addIcon: (temp164 view?) (temp164 loop?) (temp164 cel?) 0 0
										init:
									)
								)
								(break)
							)
							(= gCastFirst (gCast next: gCastFirst))
						)
					)
					(12288 (PolyEdit doit:))
					(5376
						(= gRoomObstacles (gRoom obstacles?))
						(if gRoomObstacles
							(gRoomObstacles eachElementDo: 96 drawPoly)
							(Graph 12 0 0 190 320 1)
						)
					)
					(11776
						(localproc_002e)
						(Show 4)
					)
					(4608
						(Format
							@temp0
							10
							2
							(gEgo name?)
							(gEgo view?)
							(gEgo loop?)
							(gEgo cel?)
							(gEgo x?)
							(gEgo y?)
							(gEgo z?)
							(gEgo heading?)
							(gEgo priority?)
							(gEgo signal?)
							(gEgo illegalBits?)
							(gEgo onControl:)
							(gEgo onControl: 1)
						)
						(Print
							addText: @temp0
							addIcon: (gEgo view?) (gEgo loop?) (gEgo cel?)
							init:
						)
					)
					(8704
						(= temp0 0)
						(GetInput @temp0 6 {Variable No.})
						(= gCastFirst (ReadNumber @temp0))
						(= temp0 0)
						(GetInput @temp0 6 {Value})
						(= [gEgo gCastFirst] (ReadNumber @temp0))
						(= temp0 0)
					)
					(8960
						(= temp0 0)
						(Print
							addText: {Global number:}
							addEdit: @temp0 6 0 12
							init:
						)
						(= gCastFirst (ReadNumber @temp0))
						(if (IsObject [gEgo gCastFirst])
							(Format
								@temp0
								{ Global %d: %s_}
								gCastFirst
								([gEgo gCastFirst] name?)
							)
						else
							(Format
								@temp0
								{ Global %d: %d_}
								gCastFirst
								[gEgo gCastFirst]
							)
						)
						(Prints @temp0)
					)
					(5888 (dInvD doit:))
					(9216
						(= gCastFirst 0)
						(while (< gCastFirst (gCast size?))
							(= temp164 (gCast at: gCastFirst))
							(if (not (& (temp164 signal?) $0004))
								(Format
									@temp0
									10
									1
									((temp164 -super-?) name?)
									(temp164 view?)
									(temp164 loop?)
									(temp164 cel?)
									(temp164 x?)
									(temp164 y?)
									(temp164 z?)
									(temp164 heading?)
									(temp164 priority?)
									(temp164 signal?)
									(if (temp164 isKindOf: Actor)
										(temp164 illegalBits?)
									else
										-1
									)
								)
								(Print
									addText:
										@temp0
										(CelWide
											(temp164 view?)
											(temp164 loop?)
											(temp164 cel?)
										)
										0
									window: SysWindow
									addTitle: (temp164 name?)
									addIcon: (temp164 view?) (temp164 loop?) (temp164 cel?) 0 0
									init:
								)
							)
							(++ gCastFirst)
						)
					)
					(9472
						(= gRoomObstacles (GetPort))
						(SetPort 0)
						(= temp171 5)
						(= temp172 16)
						(= temp167 15)
						(= temp168 80)
						(= temp170 (+ temp167 (* 34 temp171)))
						(= temp169 (+ temp168 (* 10 temp172)))
						(= temp165 (Graph 7 temp167 temp168 temp170 temp169 1))
						(Graph 11 temp167 temp168 temp170 temp169 1 255)
						(= temp166 0)
						(while (< temp166 256)
							(Graph
								11
								(+ temp167 temp171 (* temp171 (/ temp166 8)))
								(+ temp168 temp172 (* 16 (mod temp166 8)))
								(+ temp167 temp171 temp171 (* temp171 (/ temp166 8)))
								(+ temp168 temp172 temp172 (* temp172 (mod temp166 8)))
								1
								temp166
							)
							(++ temp166)
						)
						(Graph 12 temp167 temp168 temp170 temp169 1)
						(repeat
							(= newEvent (Event new:))
							(if
							(or (== (newEvent type?) 1) (== (newEvent type?) 4))
								(break)
							)
							(newEvent dispose:)
						)
						(newEvent dispose:)
						(Graph 8 temp165)
						(Graph 12 temp167 temp168 temp170 temp169 1)
						(SetPort gRoomObstacles)
					)
					(8192 (DialogEditor doit:))
					(9728
						(= temp0 0)
						(= gCastFirst (GetNumber {Flag No.}))
						(Bset gCastFirst)
					)
					(12800
						(= temp0 0)
						(= gCastFirst (GetNumber {Flag No.}))
						(Bclear gCastFirst)
					)
					(12544
						(= temp0 0)
						(= gCastFirst (GetNumber {Flag No.}))
						(if (Btest gCastFirst)
							(Prints {TRUE})
						else
							(Prints {FALSE})
						)
					)
					(6400
						(localproc_002e)
						(Show 2)
					)
					(4096 (gGame detailLevel: 1))
					(4864
						(Format
							@temp0
							10
							3
							(gRoom name?)
							gRoomNumber
							(gRoom curPic?)
							(gRoom style?)
							(gRoom horizon?)
							(gRoom north?)
							(gRoom south?)
							(gRoom east?)
							(gRoom west?)
							(if (IsObject (gRoom script?))
								((gRoom script?) name?)
							else
								{..none..}
							)
						)
						(Print width: 120 addText: @temp0 init:)
						(gGame showMem:)
					)
					(7936
						(= temp0 0)
						(if
							(Print
								addText: {Which Format?}
								addButton: 0 {String} 0 12
								addButton: 1 {Message} 50 12
								init:
							)
							(= temp174 (GetNumber {Noun?} 0))
							(= temp175 (GetNumber {Verb?} 0))
							(= temp176 (GetNumber {Case?} 0))
							(= temp177 (GetNumber {Sequence?} 0))
							(Message 0 temp174 temp175 temp176 temp177 @temp0)
						else
							(GetInput @temp0 50 {String to display?})
						)
						(= temp167 (GetNumber {Y Parameter?} 0))
						(= temp168 (GetNumber {X Parameter?} 0))
						(= gCastFirst (GetNumber {Box Width?} 0))
						(= theGFont (GetNumber {Font Number?} 0))
						(if (not theGFont) (= theGFont gFont))
						(Print
							posn: temp168 temp167
							width: gCastFirst
							font: theGFont
							addText: @temp0
							init:
						)
					)
					(5120
						(if gDialog (gDialog dispose:))
						(Print
							addText: {Which room do you want?}
							addEdit: @temp0 6 115 35
							init:
						)
						(if
						(and temp0 (> (= gCastFirst (ReadNumber @temp0)) 0))
							(gRoom newRoom: gCastFirst)
						)
					)
					(5632
						(User canInput: 1 canControl: 1)
						(gIconBar enable: 0 1 2 3 5 6)
					)
					(4352 (FeatureWriter doit:))
					(11520 (= gQuitGame 1))
					(12032
						(localproc_002e)
						(Show 1)
					)
					(8448
						(= temp167 0)
						(while (< temp167 (gCast size?))
							(Graph
								11
								((gCast at: temp167) brTop?)
								((gCast at: temp167) brLeft?)
								((gCast at: temp167) brBottom?)
								((gCast at: temp167) brRight?)
								1
								gColorWindowForeground
								-1
								-1
							)
							(++ temp167)
						)
					)
					(11264 (= gQuitGame 1))
					(63
						(Prints
							{Debug options:______(Page 1 of 5)\n\n___A - Show cast\n___B - Polygon editor\n___C - Show control map\n___D - Dialog editor\n___E - Show ego info\n___F - Show feature outlines\n___G - Set global\n}
						)
						(Prints
							{Debug options:______(Page 2 of 5)\n\n___H - Show global\n___I - Get inventory item\n___J - Justify text on screen\n___K - Show palette\n___L - Set flag\n___M - Clear flag\n___N - Show flag\n}
						)
						(Prints
							{Debug options:______(Page 3 of 5)\n\n___P - Show priority map\n___Q - Set Detail to 1\n___R - Show room info/free memory\n___S - Show a string or message\n___T - Teleport\n___U - Give HandsOn\n}
						)
						(Prints
							{Debug options:______(Page 4 of 5)\n\n___V - Show visual map\n___W - Feature writer\n___Y - View obstacles\n___X,Z - Quick quit\n}
						)
						(Prints
							{Debug options:______(Page 5 of 5)\n\n__A=Alt, C=Ctrl, L=Left shift, R=Right shift\n\n__Left click:\n____A_______Move ego\n____CL______Show ego\n____CR______Show room\n____CA______Show position\n}
						)
					)
					(else  (param1 claimed: 0))
				)
			)
			(1
				(switch (param1 modifiers?)
					((| $0004 $0008)
						(param1 claimed: 1)
						(Format @temp0 10 4 (param1 x?) (param1 y?))
						(= gRoomObstacles
							(Print
								posn: 160 10
								font: 999
								modeless: 1
								addText: @temp0
								init:
							)
						)
						(while (!= 2 ((= newEvent (Event new:)) type?))
							(newEvent dispose:)
						)
						(newEvent dispose:)
						(gRoomObstacles dispose:)
					)
					((| $0004 $0001)
						(param1 type: 4 message: 4864)
						(self handleEvent: param1)
					)
					((| $0004 $0002)
						(param1 type: 4 message: 4608)
						(self handleEvent: param1)
					)
					(8
						(param1 claimed: 1)
						(= temp178 (gGame setCursor: 996))
						(= userAlterEgo (User alterEgo?))
						(= gCastFirst (userAlterEgo signal?))
						(userAlterEgo startUpd:)
						(while (!= 2 ((= newEvent (Event new:)) type?))
							(userAlterEgo x: (newEvent x?) y: (- (newEvent y?) 10))
							(Animate (gCast elements?) 0)
							(newEvent dispose:)
						)
						(newEvent dispose:)
						(gGame setCursor: temp178)
						(userAlterEgo signal: gCastFirst)
					)
				)
			)
		)
	)
)

(instance drawPoly of Code
	(properties)
	
	(method (doit param1 &tmp temp0 temp1 temp2 temp3 temp4 temp5 temp6 temp7)
		(= temp5 (param1 points?))
		(= temp0 0)
		(while (< temp0 (param1 size?))
			(= temp6 (+ temp5 (* temp0 4)))
			(= temp1 (Memory 5 temp6))
			(= temp2 (Memory 5 (+ temp6 2)))
			(= temp7
				(+ temp5 (* (mod (+ temp0 1) (param1 size?)) 4))
			)
			(= temp3 (Memory 5 temp7))
			(= temp4 (Memory 5 (+ temp7 2)))
			(Graph 4 temp2 temp1 temp4 temp3 15 -1 -1)
			(++ temp0)
		)
	)
)

(instance dInvD of Dialog
	(properties)
	
	(method (init &tmp temp0 temp1 temp2 temp3 newDText gInvFirst temp6)
		(= temp1 4)
		(= temp0 temp1)
		(= temp2 temp0)
		(= temp3 0)
		(= gInvFirst (gInv first:))
		(while gInvFirst
			(= temp6 (NodeValue gInvFirst))
			(++ temp3)
			(if (temp6 isKindOf: InventoryItem)
				(= newDText (DText new:))
				(self
					add:
						(newDText
							value: temp6
							text: (temp6 name?)
							nsLeft: temp0
							nsTop: temp1
							state: 3
							font: gSmallFont
							setSize:
							yourself:
						)
				)
			)
			(if
			(< temp2 (- (newDText nsRight?) (newDText nsLeft?)))
				(= temp2 (- (newDText nsRight?) (newDText nsLeft?)))
			)
			(+= temp1
				(+ (- (newDText nsBottom?) (newDText nsTop?)) 1)
			)
			(if (> temp1 140)
				(= temp1 4)
				(+= temp0 (+ temp2 10))
				(= temp2 0)
			)
			(= gInvFirst (gInv next: gInvFirst))
		)
		(= window gWindow)
		(self setSize:)
		(= newDButton (DButton new:))
		(newDButton
			text: {All Done!}
			setSize:
			moveTo: (- nsRight (+ 4 (newDButton nsRight?))) nsBottom
		)
		(newDButton
			move: (- (newDButton nsLeft?) (newDButton nsRight?)) 0
		)
		(self add: newDButton setSize: center:)
		(return temp3)
	)
	
	(method (doit &tmp theNewDButton)
		(self init:)
		(self open: 4 15)
		(= theNewDButton newDButton)
		(repeat
			(= theNewDButton (super doit: theNewDButton))
			(if
				(or
					(not theNewDButton)
					(== theNewDButton -1)
					(== theNewDButton newDButton)
				)
				(break)
			)
			(gEgo get: (gInv indexOf: (theNewDButton value?)))
		)
		(self eachElementDo: 111 1 dispose:)
	)
	
	(method (handleEvent param1 &tmp temp0 temp1)
		(= temp0 (param1 message?))
		(switch (= temp1 (param1 type?))
			(4
				(switch temp0
					(18432 (= temp0 3840))
					(20480 (= temp0 9))
				)
			)
			(64
				(switch temp0
					(1 (= temp0 3840) (= temp1 4))
					(5 (= temp0 9) (= temp1 4))
				)
			)
		)
		(param1 type: temp1 message: temp0)
		(super handleEvent: param1)
	)
)

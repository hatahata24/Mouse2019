/*
 * drive.c
 *
 *  Created on: 2017/10/23
 *      Author: Blue
 */

#include "global.h"


//+++++++++++++++++++++++++++++++++++++++++++++++
//drive_init
// 走行系の変数の初期化，モータードライバ関係のGPIO設定とPWM出力に使うタイマの設定をする
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void drive_init(void){

	//====走行系の変数の初期化====
	max_t_cnt = MAX_T_CNT;			//テーブルカウンタ最高値初期化     MAX_T_CNTはparams.hにマクロ定義あり
	min_t_cnt = MIN_T_CNT;			//テーブルカウンタ最低値初期化     MIN_T_CNTはparams.hにマクロ定義あり
	MF.FLAGS = 0;					//フラグクリア


	//====モータードライバ関係のGPIO設定====
	pin_dir(PIN_M3, OUT);
	pin_dir(PIN_CW_R, OUT);
	pin_dir(PIN_CW_L, OUT);

	pin_write(PIN_M3, HIGH);		//ステッピングモータ励磁OFF

	drive_set_dir(FORWARD);			//前進するようにモータの回転方向を設定


	//====PWM出力に使うタイマの設定====
	/*--------------------------------------------------------------------
		TIM16 : 16ビットタイマ。左モータの制御に使う。出力はTIM16_CH1(PB4)
	--------------------------------------------------------------------*/
	__HAL_RCC_TIM16_CLK_ENABLE();

	TIM16->CR1 = 0;						//タイマ無効
	TIM16->CR2 = 0;
	TIM16->DIER = TIM_DIER_UIE;			//タイマ更新割り込みを有効に
	TIM16->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;	//PWMモード1
	TIM16->CCER = TIM_CCER_CC1E;		//TIM16_CH1出力をアクティブHighに
	TIM16->BDTR = TIM_BDTR_MOE;			//PWM出力を有効に

	TIM16->CNT = 0;						//タイマカウンタ値を0にリセット
	TIM16->PSC = 63;					//タイマのクロック周波数をシステムクロック/64=1MHzに設定
	TIM16->ARR = DEFAULT_INTERVAL;		//タイマカウンタの上限値。取り敢えずDEFAULT_INTERVAL(params.h)に設定
	TIM16->CCR1 = 25;					//タイマカウンタの比較一致値

	TIM16->EGR = TIM_EGR_UG;			//タイマ設定を反映させるためにタイマ更新イベントを起こす

	NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);			//タイマ更新割り込みハンドラを有効に
	NVIC_SetPriority(TIM1_UP_TIM16_IRQn, 2);	//タイマ更新割り込みの割り込み優先度を設定

	pin_set_alternate_function(PB4, 1);			//PB4 : TIM16_CH1はAF1に該当

	/*--------------------------------------------------------------------
		TIM17 : 16ビットタイマ。右モータの制御に使う。出力はTIM17_CH1(PB5)
	--------------------------------------------------------------------*/
	__HAL_RCC_TIM17_CLK_ENABLE();

	TIM17->CR1 = 0;						//タイマ無効
	TIM17->CR2 = 0;
	TIM17->DIER = TIM_DIER_UIE;			//タイマ更新割り込みを有効に
	TIM17->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;	//PWMモード1
	TIM17->CCER = TIM_CCER_CC1E;		//TIM16_CH1出力をアクティブHighに
	TIM17->BDTR = TIM_BDTR_MOE;			//PWM出力を有効に

	TIM17->CNT = 0;						//タイマカウンタ値を0にリセット
	TIM17->PSC = 63;					//タイマのクロック周波数をシステムクロック/64=1MHzに設定
	TIM17->ARR = DEFAULT_INTERVAL;		//タイマカウンタの上限値。取り敢えずDEFAULT_INTERVAL(params.h)に設定
	TIM17->CCR1 = 25;					//タイマカウンタの比較一致値

	TIM17->EGR = TIM_EGR_UG;			//タイマ設定を反映させるためにタイマ更新イベントを起こす

	NVIC_EnableIRQ(TIM1_TRG_COM_TIM17_IRQn);			//タイマ更新割り込みハンドラを有効に
	NVIC_SetPriority(TIM1_TRG_COM_TIM17_IRQn, 2);		//タイマ更新割り込みの割り込み優先度を設定

	pin_set_alternate_function(PB5, 10);	//PB5 : TIM17_CH1はAF10に該当
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//drive_enable_motor
// ステッピングモータを励磁する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void drive_enable_motor(void){
	pin_write(PIN_M3, LOW);			//ステッピングモータ励磁ON
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//drive_disable_motor
// ステッピングモータの励磁を切る
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void drive_disable_motor(void){
	pin_write(PIN_M3, HIGH);		//ステッピングモータ励磁OFF
}


/*----------------------------------------------------------
		16bitタイマ割り込み
----------------------------------------------------------*/
/**********
---走行の仕組み---
ステッピングモータの動作制御は16bitタイマで行われている。各16bitタイマの設定は，
・カウント開始からCCR1までの間は出力ピンがLowになる
・CCR1に達すると出力ピンがHighになる
・ARRに達すると割り込みを生成+タイマカウンタをリセット
となっている（drive_init函数参照）
モータドライバの（RefをHighにした状態で）ClockをHighにすることで一定角度回転し，
Lowに戻した後またHighにすることでまた一定角度回転する。
Clockにはタイマの出力ピンを繋いであるので，タイマの周期を速くするほど回転も速くなる。
このプログラムではカウント開始からCCR1の間を一定にして（モータドライバがHighを認識する分長さが必要），
カウント開始からARRの間を調整することで速度を変化させている。
加減速等の状態はMF.FLAG構造体（global.hで定義）で管理されている。
加減速について，事前にExcelで計算した時間（割り込み）ごとのARRの値をtable[]配列に記録しておく。
（配列の値はtable.hにExcelからコピー&ペーストをして設定する。table[]の定義はdrive.h参照）
今加減速のどの段階なのか（table[]の要素番号・インデックス）はt_cnt_l,t_cnt_rで記録している。
**********/

//+++++++++++++++++++++++++++++++++++++++++++++++
//TIM1_UP_TIM16_IRQHandler
// 16ビットタイマーTIM16の割り込み関数，左モータの管理を行う
// 引数：無し
// 戻り値：無し
//+++++++++++++++++++++++++++++++++++++++++++++++
void TIM1_UP_TIM16_IRQHandler(){

	if( !(TIM16->SR & TIM_SR_UIF) ){
		return;
	}

	pulse_l++;															//左パルスのカウンタをインクリメント

	//====加減速処理====
	//----減速処理----
	if(MF.FLAG.DECL){													//減速フラグが立っている場合
		t_cnt_l = max(t_cnt_l - 1, min_t_cnt);
	}
	//----加速処理----
	else if(MF.FLAG.ACCL){												//加速フラグが立っている場合
		t_cnt_l = min(t_cnt_l + 1, max_t_cnt);
	}

	//----デフォルトインターバル----
	if(MF.FLAG.DEF){													//デフォルトインターバルフラグが立っている場合
		TIM16->ARR = DEFAULT_INTERVAL - dl;								//デフォルトインターバルに制御を加えた値に設定
	}
	//----それ以外の時はテーブルカウンタの指し示すインターバル----
	else {
		TIM16->ARR = table[t_cnt_l] - dl;								//左モータインターバル設定
	}

	TIM16->SR &= ~TIM_SR_UIF;
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//TIM1_TRG_CON_TIM17_IRQHandler
// 16ビットタイマーTIM17の割り込み関数，右モータの管理を行う
// 引数：無し
// 戻り値：無し
//+++++++++++++++++++++++++++++++++++++++++++++++
void TIM1_TRG_COM_TIM17_IRQHandler(){

	if( !(TIM17->SR & TIM_SR_UIF) ){
		return;
	}

	pulse_r++;															//右パルスのカウンタをインクリメント

	//====加減速処理====
	//----減速処理----
	if(MF.FLAG.DECL){													//減速フラグが立っている場合
		t_cnt_r = max(t_cnt_r - 1, min_t_cnt);
	}
	//----加速処理----
	else if(MF.FLAG.ACCL){												//加速フラグが立っている場合
		t_cnt_r = min(t_cnt_r + 1, max_t_cnt);
	}

	//----デフォルトインターバル----
	if(MF.FLAG.DEF){													//デフォルトインターバルフラグが立っている場合
		TIM17->ARR = DEFAULT_INTERVAL - dr;								//デフォルトインターバルに制御を加えた値に設定
	}
	//----それ以外の時はテーブルカウンタの指し示すインターバル----
	else {
		TIM17->ARR = table[t_cnt_r] - dr;								//右モータインターバル設定
	}

	TIM17->SR &= ~TIM_SR_UIF;
}



//+++++++++++++++++++++++++++++++++++++++++++++++
//drive_reset_t_cnt
// テーブルカウンタをリセット（min_t_cntの値にセット）する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void drive_reset_t_cnt(void){
	t_cnt_l = t_cnt_r = min_t_cnt;
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//drive_start
// 走行を開始する
// （pulse_l,pulse_rを0にリセットしてタイマを有効にする）
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void drive_start(void){

	pulse_l = pulse_r = 0;		//走行したパルス数の初期化
	TIM16->CR1 |= TIM_CR1_CEN;	// Enable timer
	TIM17->CR1 |= TIM_CR1_CEN;	// Enable timer
}

//+++++++++++++++++++++++++++++++++++++++++++++++
//drive_stop
// 走行を終了する
// （タイマを止めてタイマカウント値を0にリセットする）
// 引数1：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void drive_stop(void){

	TIM16->CR1 &= ~TIM_CR1_CEN;	// Disable timer
	TIM17->CR1 &= ~TIM_CR1_CEN;	// Disable timer
	TIM16->CNT = 0;				// Reset Counter
	TIM17->CNT = 0;				// Reset Counter
}

//+++++++++++++++++++++++++++++++++++++++++++++++
//drive_set_dir
// 進行方向を設定する
// 引数1：d_dir …… どの方向に進行するか  0桁目で左，1桁目で右の方向設定
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void drive_set_dir(uint8_t d_dir){

	//====左モータ====
	switch(d_dir & 0x0f){									//0~3ビット目を取り出す
		//----正回転----
		case 0x00:											//0x00の場合
			pin_write(PIN_CW_L, MT_FWD_L);					//左を前進方向に設定
			break;
		//----逆回転----
		case 0x01:								 			//0x01の場合
			pin_write(PIN_CW_L, MT_BACK_L);					//左を後進方向に設定
			break;
	}
	//====右モータ====
	switch(d_dir & 0xf0){									//4~7ビット目を取り出す
		case 0x00:											//0x00の場合
			pin_write(PIN_CW_R, MT_FWD_R);					//右を前進方向に設定
			break;
		case 0x10:											//0x10の場合
			pin_write(PIN_CW_R, MT_BACK_R);					//右を後進方向に設定
			break;
	}
}




/*==========================================================
		走行系関数
==========================================================*/
/*
		基本仕様として，
		基幹関数		第一引数:走行パルス数

		マウスフラグ(MF)
			6Bit:デフォルトインターバルフラグ
			5Bit:減速フラグ
			4Bit:加速フラグ
			3Bit:制御フラグ
			1Bit:二次走行フラグ
*/
/*----------------------------------------------------------
		基幹関数
----------------------------------------------------------*/
//+++++++++++++++++++++++++++++++++++++++++++++++
//driveA
// 指定パルス分加速走行する
// 引数1：dist …… 走行するパルス
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void driveA(uint16_t dist){

	//====走行====
	//----走行開始----
	MF.FLAG.DECL = 0;
	MF.FLAG.DEF = 0;
	MF.FLAG.ACCL = 1;										//減速・デフォルトインターバルフラグをクリア，加速フラグをセット
	drive_reset_t_cnt();									//テーブルカウンタをリセット
	drive_start();											//走行開始

	//----走行----
	while((pulse_l < dist) || (pulse_r < dist));			//左右のモータが指定パルス以上進むまで待機

	drive_stop();
}

//+++++++++++++++++++++++++++++++++++++++++++++++
//driveD
// 指定パルス分減速走行して停止する
// 引数1：dist …… 走行するパルス
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void driveD(uint16_t dist){

	//====走行====
	//----走行開始----
	MF.FLAG.DECL = 0;
	MF.FLAG.DEF = 0;
	MF.FLAG.ACCL = 0;										//加速・減速・デフォルトインターバルフラグをクリア
	drive_start();											//痩躯開始

	int16_t c_pulse = dist - (t_cnt_l - min_t_cnt);			//等速走行距離 = 総距離 - 減速に必要な距離
	if(c_pulse > 0){
		//----等速走行----
		while((pulse_l < c_pulse) || (pulse_r < c_pulse));	//左右のモータが等速分のパルス以上進むまで待機
	}

	//----減速走行----
	MF.FLAG.DECL = 1;										//減速フラグをセット
	while((pulse_l < dist) || (pulse_r < dist));			//左右のモータが減速分のパルス以上進むまで待機

	//====走行終了====
	drive_stop();
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//driveU
// 指定パルス分等速走行して停止する
// 引数1：dist …… 走行するパルス
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void driveU(uint16_t dist){

	//====等速走行開始====
	MF.FLAG.DECL = 0;
	MF.FLAG.DEF = 0;
	MF.FLAG.ACCL = 0;										//加速・減速・デフォルトインターバルフラグをクリア
	drive_start();											//走行開始

	//====走行====
	while((pulse_l < dist) || (pulse_r < dist));			//左右のモータが減速分のパルス以上進むまで待機

	//====走行終了====
	drive_stop();
}

//+++++++++++++++++++++++++++++++++++++++++++++++
//driveC
// 指定パルス分デフォルトインターバルで走行して停止する
// 引数1：dist …… 走行するパルス
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void driveC(uint16_t dist){

	//====回転開始====
	MF.FLAG.DECL = 0;
	MF.FLAG.DEF = 1;
	MF.FLAG.ACCL = 0;										//加速・減速フラグをクリア，デフォルトインターバルフラグをセット
	drive_start();											//走行開始

	//====回転====
	while((pulse_l < dist) || (pulse_r < dist));			//左右のモータが定速分のパルス以上進むまで待機

	drive_stop();
}


/*----------------------------------------------------------
		上位関数
----------------------------------------------------------*/
//+++++++++++++++++++++++++++++++++++++++++++++++
//half_sectionA
// 半区画分加速しながら走行する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void half_sectionA(void){

	MF.FLAG.CTRL = 1;										//制御を有効にする
	driveA(PULSE_SEC_HALF);									//半区画のパルス分加速しながら走行。走行後は停止しない
	get_wall_info();										//壁情報を取得，片壁制御の有効・無効の判断
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//half_sectionD
// 半区画分減速しながら走行し停止する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void half_sectionD(void){

	MF.FLAG.CTRL = 1;										//制御を有効にする
	driveD(PULSE_SEC_HALF);									//半区画のパルス分減速しながら走行。走行後は停止する
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//one_section
// 1区画分進んで停止する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void one_section(void){

	half_sectionA();										//半区画分加速走行
	half_sectionD();										//半区画分減速走行のち停止
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//one_sectionU
// 等速で1区画分進む
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void one_sectionU(void){

	MF.FLAG.CTRL = 1;										//制御を有効にする
	driveU(PULSE_SEC_HALF);									//半区画のパルス分等速走行。走行後は停止しない
	driveU(PULSE_SEC_HALF);									//半区画のパルス分等速走行。走行後は停止しない
	get_wall_info();										//壁情報を取得
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//rotate_R90
// 右に90度回転する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void rotate_R90(void){

	MF.FLAG.CTRL = 0;										//制御無効
	drive_set_dir(ROTATE_R);								//右に旋回するようモータの回転方向を設定
	drive_wait();
	driveC(PULSE_ROT_R90);									//デフォルトインターバルで指定パルス分回転。回転後に停止する
	drive_wait();
	drive_set_dir(FORWARD);									//前進するようにモータの回転方向を設定
}

//+++++++++++++++++++++++++++++++++++++++++++++++
//rotate_L90
// 左に90度回転する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void rotate_L90(void){

	MF.FLAG.CTRL = 0;										//制御を無効にする
	drive_set_dir(ROTATE_L);								//左に旋回するようモータの回転方向を設定
	drive_wait();
	driveC(PULSE_ROT_L90);									//デフォルトインターバルで指定パルス分回転。回転後に停止する
	drive_wait();
	drive_set_dir(FORWARD);									//前進するようにモータの回転方向を設定
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//rotate_180
// 180度回転する
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void rotate_180(void){

	MF.FLAG.CTRL = 0;										//制御を無効にする
	drive_set_dir(ROTATE_R);								//左に旋回するようモータの回転方向を設定
	drive_wait();
	driveC(PULSE_ROT_180);									//デフォルトインターバルで指定パルス分回転。回転後に停止する
	drive_wait();
	drive_set_dir(FORWARD);									//前進するようにモータの回転方向を設定
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//set_position
// 機体の尻を壁に当てて場所を区画中央に合わせる
// 引数：sw …… 0以外ならget_base()する
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void set_position(uint8_t sw){

	MF.FLAG.CTRL = 0;										//制御を無効にする
	drive_set_dir(BACK);									//後退するようモータの回転方向を設定
	drive_wait();
	driveC(PULSE_SETPOS_BACK);								//尻を当てる程度に後退。回転後に停止する
	drive_wait();
	if(sw){
		get_base();
	}
	drive_set_dir(FORWARD);									//前進するようにモータの回転方向を設定
	drive_wait();
	driveC(PULSE_SETPOS_SET);								//デフォルトインターバルで指定パルス分回転。回転後に停止する
	drive_wait();
}


//+++++++++++++++++++++++++++++++++++++++++++++++
//test_run
// テスト走行モード
// 引数：なし
// 戻り値：なし
//+++++++++++++++++++++++++++++++++++++++++++++++
void test_run(void){

	int mode = 0;
	printf("Test Run, Mode : %d\n", mode);
	drive_enable_motor();

	while(1){

		led_write(mode & 0b001, mode & 0b010, mode & 0b100);
		if( is_sw_pushed(PIN_SW_INC) ){
			ms_wait(100);
			while( is_sw_pushed(PIN_SW_INC) );
			mode++;
			if(mode > 7){
				mode = 0;
			}
			printf("Test Run, Mode : %d\n", mode);
		}
		if( is_sw_pushed(PIN_SW_DEC) ){
			ms_wait(100);
			while( is_sw_pushed(PIN_SW_DEC) );
			mode--;
			if(mode < 0){
				mode = 7;
			}
			printf("Test Run, Mode : %d\n", mode);
		}

		if( is_sw_pushed(PIN_SW_RET) ){
			ms_wait(100);
			while( is_sw_pushed(PIN_SW_RET) );
			int i;
			switch(mode){

				case 0:
					//----尻当て----
					printf("Set Position.\n");
					set_position(0);
					break;
				case 1:
					//----6区画等速走行----
					printf("6 Section, Forward, Constant Speed.\n");
					MF.FLAG.CTRL = 0;				//制御を無効にする
					drive_set_dir(FORWARD);			//前進するようにモータの回転方向を設定
					for(i = 0; i < 6; i++){
						driveC(PULSE_SEC_HALF*2);	//一区画のパルス分デフォルトインターバルで走行
						drive_wait();
					}
					break;
				case 2:
					//----右90度回転----
					printf("Rotate R90.\n");
					for(i = 0; i < 16; i++){
						rotate_R90();
					}
					break;
				case 3:
					//----左90度回転----
					printf("Rotate L90.\n");
					for(i = 0; i < 16; i++){
						rotate_L90();
					}
					break;
				case 4:
					//----180度回転----
					printf("Rotate 180.\n");
					for(i = 0; i < 8; i++){
						rotate_180();
					}
					break;
				case 5:
					break;
				case 6:
					break;
				case 7:
					//----6区画連続走行----
					printf("6 Section, Forward, Continuous.\n");
					MF.FLAG.CTRL = 0;				//制御を無効にする
					drive_set_dir(FORWARD);			//前進するようにモータの回転方向を設定
					driveA(PULSE_SEC_HALF);			//半区画のパルス分加速しながら走行
					for(i = 0; i < 6-1; i++){
						driveU(PULSE_SEC_HALF*2);	//一区画のパルス分等速走行
					}
					driveD(PULSE_SEC_HALF);			//半区画のパルス分減速しながら走行。走行後は停止する
					break;
			}
		}
	}
	drive_disable_motor();

}

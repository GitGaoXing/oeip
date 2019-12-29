#include "detection_layer.h"
#include "activations.h"
#include "softmax_layer.h"
#include "blas.h"
#include "box.h"
#include "cuda.h"
#include "utils.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>


//�����ڸ�����������˵,n=3/classes=20/coords=4
//һ��batch���35(classes+(1+coords)*n)��7*7(side)������ͼ��
//ǰ20(classes)������ͼ��������ͼ7*7��49�������ÿ�����Ӷ�Ӧ�ĸ�classe����
//���������ÿ�����ӱ�ʾ��Ԥ�������box�û���Ӧ����
//���12���ÿ�����ӱ�ʾ�Ķ�Ӧ����box���ĸ�ֵ
detection_layer make_detection_layer(int batch, int inputs, int n, int side, int classes, int coords, int rescore)
{
	detection_layer l = { 0 };
	l.type = DETECTION;
	//����n��ʾÿ�����Ӽ�⼸�ָ���(classes��ѡn��)
	l.n = n;
	l.batch = batch;
	//Ϊ��һ��ȫ���Ӳ������ 1715
	l.inputs = inputs;
	//��ʾһ������������
	l.classes = classes;
	//������� �ĸ�
	l.coords = coords;
	l.rescore = rescore;
	//�����������ͼ�ϵĸ�����
	l.side = side;
	l.w = side;
	l.h = side;
	//����ÿ������ÿ������Ԥ��2��bouding box��ÿ��box����5��Ԥ�������Լ�20�������ʡ�
	//�ܹ����7��7����2*5+20��=1470��tensor  /��Ӧcfg�ļ��õ�7��7����3*5+20��=1715
	assert(side*side*((1 + l.coords)*l.n + l.classes) == inputs);
	l.cost = calloc(1, sizeof(float));
	l.outputs = l.inputs;
	//��ǩֵ
	l.truths = l.side*l.side*(1 + l.coords + l.classes);
	l.output = calloc(batch*l.outputs, sizeof(float));
	l.delta = calloc(batch*l.outputs, sizeof(float));

	l.forward = forward_detection_layer;
	l.backward = backward_detection_layer;
#ifdef GPU
	l.forward_gpu = forward_detection_layer_gpu;
	l.backward_gpu = backward_detection_layer_gpu;
	l.output_gpu = cuda_make_array(l.output, batch*l.outputs);
	l.delta_gpu = cuda_make_array(l.delta, batch*l.outputs);
#endif

	fprintf(stderr, "Detection Layer\n");
	srand(0);

	return l;
}

void forward_detection_layer(const detection_layer l, network net)
{
	//���ֶ��ٸ�����
	int locations = l.side*l.side;
	int i, j;
	memcpy(l.output, net.input, l.outputs*l.batch * sizeof(float));
	//if(l.reorg) reorg(l.output, l.w*l.h, size*l.n, l.batch, 1);
	int b;
	if (l.softmax) {
		for (b = 0; b < l.batch; ++b) {
			int index = b * l.inputs;
			//ÿ������
			for (i = 0; i < locations; ++i) {
				int offset = i * l.classes;
				//��ÿ���������classes������ĸ���Ϊ1
				softmax(l.output + index + offset, l.classes, 1, 1,
					l.output + index + offset);
			}
		}
	}
	if (net.train) {
		float avg_iou = 0;
		float avg_cat = 0;
		float avg_allcat = 0;
		float avg_obj = 0;
		float avg_anyobj = 0;
		int count = 0;
		*(l.cost) = 0;
		//inputs: 7��7����3*5+20��=1715 ��¼����������ʧֵ����ͬ���Ͳ�ͬscale
		int size = l.inputs * l.batch;
		memset(l.delta, 0, size * sizeof(float));
		for (b = 0; b < l.batch; ++b) {
			int index = b * l.inputs;
			//����ÿ������ 7*7=49��
			//output/delta�ȼ�¼���и����������ͼ���,�ټ�¼���и���N�����ͻ��ʣ��ټ�¼���и���N��box��ֵ
			//������������(output/delta)7*7*20-7*7*3-7*7*3*4,(truth)7*7*(1+20+4)	
			for (i = 0; i < locations; ++i) {
				//ÿ������Ԥ�� i*(1+4+20) truthÿ����������25��ֵ
				int truth_index = (b*locations + i)*(1 + l.coords + l.classes);
				//�Ƿ�����
				int is_obj = net.truth[truth_index];
				//ѵ����box������õļ��� noobject_scale 0.5
				for (j = 0; j < l.n; ++j) {
					//���Կ������ŵ�Ԥ��ֵ���ȷ��������ͼ��ʣ�Ȼ����Ԥ���N��
					int p_index = index + locations * l.classes + i * l.n + j;
					//��¼��ֵ
					l.delta[p_index] = l.noobject_scale*(0 - l.output[p_index]);
					*(l.cost) += l.noobject_scale*pow(l.output[p_index], 2);
					avg_anyobj += l.output[p_index];
				}
				int best_index = -1;
				float best_iou = 0;
				float best_rmse = 20;

				if (!is_obj) {
					continue;
				}
				//output/delta�ȼ�¼���и����������ͼ���,�ټ�¼���и���N�����ͻ��ʣ��ټ�¼���и���N��box��ֵ
				//��classesȡ20��nȡ3��coordsȡ4
				//������������(output/delta)7*7*20-7*7*3-7*7*3*4,(truth)7*7*(1+20+4)			
				int class_index = index + i * l.classes;
				//class_scale 1
				for (j = 0; j < l.classes; ++j) {
					l.delta[class_index + j] = l.class_scale * (net.truth[truth_index + 1 + j] - l.output[class_index + j]);
					*(l.cost) += l.class_scale * pow(net.truth[truth_index + 1 + j] - l.output[class_index + j], 2);
					//������ڵ�ǰ����
					if (net.truth[truth_index + 1 + j])
						avg_cat += l.output[class_index + j];
					avg_allcat += l.output[class_index + j];
				}
				//truthÿ�����������4��ֵ��box�ĸ���
				box truth = float_to_box(net.truth + truth_index + 1 + l.classes, 1);
				truth.x /= l.side;
				truth.y /= l.side;
				//���Ԥ���N�������е�box�к���ʵ��������Ǹ�
				for (j = 0; j < l.n; ++j) {
					int box_index = index + locations * (l.classes + l.n) + (i*l.n + j) * l.coords;
					box out = float_to_box(l.output + box_index, 1);
					out.x /= l.side;
					out.y /= l.side;
					if (l.sqrt) {
						out.w = out.w*out.w;
						out.h = out.h*out.h;
					}
					//���㽻����
					float iou = box_iou(out, truth);
					//box�ı߲�
					float rmse = box_rmse(out, truth);
					//ѡ�񽻲�����õ�box,���û��,ѡ������С��box
					if (best_iou > 0 || iou > 0) {
						if (iou > best_iou) {
							best_iou = iou;
							best_index = j;
						}
					}
					else {
						if (rmse < best_rmse) {
							best_rmse = rmse;
							best_index = j;
						}
					}
				}

				if (l.forced) {
					if (truth.w*truth.h < .1) {
						best_index = 1;
					}
					else {
						best_index = 0;
					}
				}
				if (l.random && *(net.seen) < 64000) {
					best_index = rand() % l.n;
				}
				//�л�����ѡ�����box�������
				int box_index = index + locations * (l.classes + l.n) + (i*l.n + best_index) * l.coords;
				//truthÿ��������box��ֵ
				int tbox_index = truth_index + 1 + l.classes;

				box out = float_to_box(l.output + box_index, 1);
				out.x /= l.side;
				out.y /= l.side;
				if (l.sqrt) {
					out.w = out.w*out.w;
					out.h = out.h*out.h;
				}
				//����iou��ֵ
				float iou = box_iou(out, truth);

				//printf("%d,", best_index);
				//best_index��Ӧ���͵ĸ���
				int p_index = index + locations * l.classes + i * l.n + best_index;
				//��ȷ���Խ��,l.costԽС,��ֵԽ�󣬼ӵ�Խ��
				*(l.cost) -= l.noobject_scale * pow(l.output[p_index], 2);
				*(l.cost) += l.object_scale * pow(1 - l.output[p_index], 2);
				avg_obj += l.output[p_index];
				//���ԽС
				l.delta[p_index] = l.object_scale * (1. - l.output[p_index]);

				if (l.rescore) {
					l.delta[p_index] = l.object_scale * (iou - l.output[p_index]);
				}
				//coord_scale 5����noobject_scale 0.5/class_scale 1/noobject_scale 1����
				l.delta[box_index + 0] = l.coord_scale*(net.truth[tbox_index + 0] - l.output[box_index + 0]);
				l.delta[box_index + 1] = l.coord_scale*(net.truth[tbox_index + 1] - l.output[box_index + 1]);
				l.delta[box_index + 2] = l.coord_scale*(net.truth[tbox_index + 2] - l.output[box_index + 2]);
				l.delta[box_index + 3] = l.coord_scale*(net.truth[tbox_index + 3] - l.output[box_index + 3]);
				if (l.sqrt) {
					l.delta[box_index + 2] = l.coord_scale*(sqrt(net.truth[tbox_index + 2]) - l.output[box_index + 2]);
					l.delta[box_index + 3] = l.coord_scale*(sqrt(net.truth[tbox_index + 3]) - l.output[box_index + 3]);
				}

				*(l.cost) += pow(1 - iou, 2);
				//avg_iou��ʾ�ĵ�ǰ����������õ�iou�ϼ�
				avg_iou += iou;
				++count;
			}
		}

		if (0) {
			float *costs = calloc(l.batch*locations*l.n, sizeof(float));
			for (b = 0; b < l.batch; ++b) {
				int index = b * l.inputs;
				for (i = 0; i < locations; ++i) {
					for (j = 0; j < l.n; ++j) {
						int p_index = index + locations * l.classes + i * l.n + j;
						costs[b*locations*l.n + i * l.n + j] = l.delta[p_index] * l.delta[p_index];
					}
				}
			}
			int indexes[100];
			top_k(costs, l.batch*locations*l.n, 100, indexes);
			float cutoff = costs[indexes[99]];
			for (b = 0; b < l.batch; ++b) {
				int index = b * l.inputs;
				for (i = 0; i < locations; ++i) {
					for (j = 0; j < l.n; ++j) {
						int p_index = index + locations * l.classes + i * l.n + j;
						if (l.delta[p_index] * l.delta[p_index] < cutoff) l.delta[p_index] = 0;
					}
				}
			}
			free(costs);
		}


		*(l.cost) = pow(mag_array(l.delta, l.outputs * l.batch), 2);


		printf("Detection Avg IOU: %f, Pos Cat: %f, All Cat: %f, Pos Obj: %f, Any Obj: %f, count: %d\n", avg_iou / count, avg_cat / count, avg_allcat / (count*l.classes), avg_obj / count, avg_anyobj / (l.batch*locations*l.n), count);
		//if(l.reorg) reorg(l.delta, l.w*l.h, size*l.n, l.batch, 0);
	}
}

void backward_detection_layer(const detection_layer l, network net)
{
	axpy_cpu(l.batch*l.inputs, 1, l.delta, 1, net.delta, 1);
}

void get_detection_detections(layer l, int w, int h, float thresh, detection *dets)
{
	int i, j, n;
	float *predictions = l.output;
	//output/delta�ȼ�¼���и����������ͼ���,�ټ�¼���и���N�����ͻ��ʣ��ټ�¼���и���N��box��ֵ
	//������������(output/delta)7*7*20-7*7*3-7*7*3*4,(truth)7*7*(1+20+4)
	//side ��ʾ��ǰ��������϶�����ӣ������趨Ϊ7��һ����7*7=49
	for (i = 0; i < l.side*l.side; ++i) {
		int row = i / l.side;
		int col = i % l.side;
		//����n��ʾÿ�����Ӽ�⼸�ָ��ʣ���ǰ���õ�3
		for (n = 0; n < l.n; ++n) {
			int index = i * l.n + n;
			//��Ӧ��ǰ���ӵ�����ѡ������֮һ
			int p_index = l.side*l.side*l.classes + i * l.n + n;
			//���ӵĸ���
			float scale = predictions[p_index];
			//�ҵ�box��Ӧ��������
			int box_index = l.side*l.side*(l.classes + l.n) + (i*l.n + n) * 4;
			box b;
			b.x = (predictions[box_index + 0] + col) / l.side * w;
			b.y = (predictions[box_index + 1] + row) / l.side * h;
			b.w = pow(predictions[box_index + 2], (l.sqrt ? 2 : 1)) * w;
			b.h = pow(predictions[box_index + 3], (l.sqrt ? 2 : 1)) * h;
			dets[index].bbox = b;
			dets[index].objectness = scale;
			for (j = 0; j < l.classes; ++j) {
				int class_index = i * l.classes;
				float prob = scale * predictions[class_index + j];
				dets[index].prob[j] = (prob > thresh) ? prob : 0;
			}
		}
	}
}

#ifdef GPU

void forward_detection_layer_gpu(const detection_layer l, network net)
{
	if (!net.train) {
		copy_gpu(l.batch*l.inputs, net.input_gpu, 1, l.output_gpu, 1);
		return;
	}

	cuda_pull_array(net.input_gpu, net.input, l.batch*l.inputs);
	forward_detection_layer(l, net);
	cuda_push_array(l.output_gpu, l.output, l.batch*l.outputs);
	cuda_push_array(l.delta_gpu, l.delta, l.batch*l.inputs);
}

void backward_detection_layer_gpu(detection_layer l, network net)
{
	axpy_gpu(l.batch*l.inputs, 1, l.delta_gpu, 1, net.delta_gpu, 1);
	//copy_gpu(l.batch*l.inputs, l.delta_gpu, 1, net.delta_gpu, 1);
}
#endif


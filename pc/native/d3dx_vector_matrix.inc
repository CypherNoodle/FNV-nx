/* Own row-vector algebra. All guest inputs are copied before output commits. */
static int math_rounding(uint32_t out){
    if(fegetround()==FE_TONEAREST)return 1;
    recomp_unimpl("D3DX math rounding mode unsupported",out);return 0;
}
static int math_values(const float *values,unsigned count,uint32_t out){
    for(unsigned i=0;i<count;i++)if(!supported(values[i])){recomp_unimpl("D3DX math value unsupported",out);return 0;}
    return 1;
}
static void api_matrix_transpose(void){
    uint32_t out=fnv_cpu_argument_u32(0),input=fnv_cpu_argument_u32(1),src[16],dst[16];
    unsigned char *destination=checked(out,1),*source=checked(input,0);if(!destination || !source)return;
    memcpy(src,source,sizeof(src));
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)dst[r*4+c]=src[c*4+r];
    /* This is a bit permutation, including NaNs, signed zeros and denormals. */
    memcpy(destination,dst,sizeof(dst));fnv_cpu_return_u32(out,8);
}
static void vector_transform(unsigned dimensions,int coord){
    uint32_t out=fnv_cpu_argument_u32(0),input=fnv_cpu_argument_u32(1),matrix=fnv_cpu_argument_u32(2);
    unsigned char *destination=image_pointer(out,dimensions*4,1),*source=image_pointer(input,dimensions*4,0),*m=checked(matrix,0);
    if(!destination || !source || !m || !math_rounding(out))return;
    float v[4]={0,0,0,coord?1.0f:0.0f},a[16],result[4];memcpy(v,source,dimensions*4);memcpy(a,m,64);
    if(!math_values(v,4,out) || !math_values(a,16,out))return;
    for(unsigned col=0;col<4;col++){
        float p[4];for(unsigned row=0;row<4;row++)p[row]=v[row]*a[row*4+col];
        if(!math_values(p,4,out))return;
        float first=p[0]+p[1],second=p[2]+p[3];
        if(!math_values(&first,1,out) || !math_values(&second,1,out))return;
        result[col]=first+second;
    }
    if(!math_values(result,4,out))return;
    if(coord){
        if(result[3]==0){recomp_unimpl("D3DX homogeneous zero divisor unsupported",out);return;}
        for(unsigned i=0;i<3;i++)result[i]/=result[3];
        if(!math_values(result,3,out))return;
    }
    memcpy(destination,result,dimensions*4);fnv_cpu_return_u32(out,12);
}
static void api_vec4_transform(void){vector_transform(4,0);}
static void api_vec3_transform_coord(void){vector_transform(3,1);}
static void api_vec3_transform_normal(void){vector_transform(3,0);}
static void api_plane_transform(void){vector_transform(4,0);}
static void vector_normalize(unsigned dimensions){
    uint32_t out=fnv_cpu_argument_u32(0),input=fnv_cpu_argument_u32(1);
    unsigned char *destination=image_pointer(out,dimensions*4,1),*source=image_pointer(input,dimensions*4,0);
    if(!destination || !source || !math_rounding(out))return;
    float v[4]={0},result[4]={0},squares[3];memcpy(v,source,dimensions*4);
    if(!math_values(v,dimensions,out))return;
    for(unsigned i=0;i<3;i++)squares[i]=v[i]*v[i];
    if(!math_values(squares,3,out))return;
    float sum=squares[0]+squares[1];if(!math_values(&sum,1,out))return;
    sum+=squares[2];if(!math_values(&sum,1,out))return;
    if(sum==0){
        if(v[0]!=0 || v[1]!=0 || v[2]!=0 || (dimensions==4 && v[3]!=0)){
            recomp_unimpl("D3DX degenerate normalization unsupported",out);return;
        }
    }else{
        float length=sqrtf(sum);if(!math_values(&length,1,out))return;
        for(unsigned i=0;i<dimensions;i++)result[i]=v[i]/length;
        if(!math_values(result,dimensions,out))return;
    }
    memcpy(destination,result,dimensions*4);fnv_cpu_return_u32(out,8);
}
static void api_vec3_normalize(void){vector_normalize(3);}
static void api_plane_normalize(void){vector_normalize(4);}
static void api_matrix_inverse(void){
    uint32_t out=fnv_cpu_argument_u32(0),det_out=fnv_cpu_argument_u32(1),input=fnv_cpu_argument_u32(2);
    unsigned char *destination=checked(out,1),*source=checked(input,0),*det_pointer=det_out?image_pointer(det_out,4,1):NULL;
    if(!destination || !source || (det_out && !det_pointer) || !math_rounding(out))return;
    if(det_out && (((uint64_t)det_out< (uint64_t)out+64 && (uint64_t)det_out+4>out) ||
                  ((uint64_t)det_out< (uint64_t)input+64 && (uint64_t)det_out+4>input))){
        recomp_unimpl("D3DX determinant alias unsupported",det_out);return;
    }
    float original[16],result[16];double work[4][8],det=1,norm=0;memcpy(original,source,64);
    if(!math_values(original,16,out))return;
    for(unsigned r=0;r<4;r++){
        double sum=0;for(unsigned c=0;c<4;c++){work[r][c]=original[r*4+c];work[r][c+4]=r==c;sum+=fabs(work[r][c]);}
        if(sum>norm)norm=sum;
    }
    for(unsigned col=0;col<4;col++){
        unsigned pivot=col;for(unsigned r=col+1;r<4;r++)if(fabs(work[r][col])>fabs(work[pivot][col]))pivot=r;
        /* The x86 D3DX DLL leaves both outputs untouched for a singular input.
         * Its x64 counterpart writes a zero determinant; the guest is x86. */
        if(work[pivot][col]==0){fnv_cpu_return_u32(0,12);return;}
        if(pivot!=col){for(unsigned c=0;c<8;c++){double temp=work[col][c];work[col][c]=work[pivot][c];work[pivot][c]=temp;}det=-det;}
        double value=work[col][col];det*=value;for(unsigned c=0;c<8;c++)work[col][c]/=value;
        for(unsigned r=0;r<4;r++)if(r!=col){double factor=work[r][col];for(unsigned c=0;c<8;c++)work[r][c]-=factor*work[col][c];}
    }
    double inverse_norm=0;for(unsigned r=0;r<4;r++){
        double sum=0;for(unsigned c=0;c<4;c++){double value=work[r][c+4];sum+=fabs(value);result[r*4+c]=(float)value;}
        if(sum>inverse_norm)inverse_norm=sum;
    }
    float determinant=(float)det;
    if(!isfinite(norm*inverse_norm) || norm*inverse_norm>1.0e12 || determinant==0 || !supported(determinant)){
        recomp_unimpl("D3DX inverse conditioning or determinant unsupported",out);return;
    }
    if(!math_values(result,16,out))return;
    memcpy(destination,result,64);if(det_pointer)memcpy(det_pointer,&determinant,4);fnv_cpu_return_u32(out,12);
}

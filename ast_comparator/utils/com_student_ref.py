import os
import subprocess
import numpy as np
from graphviz import Digraph

#PATH="/home/gao/Documents/tiny-comparator/ast_comparator/"
PATH=os.path.dirname(os.path.realpath(__file__))
PATH=PATH[0:len(PATH)-5]
SIGMOID=0
num_student=63
num_prev=23
num_ref=39
num_scope=0
scope=[]    # string list
threshold=0.90  # threshold for a single scope used to calculate overall similarity
overall_threshold=0.7   # threshold used to evaluate whether overall similarity proves plagiarism
sigma = 2
default_conformance=0.5 # when some scope has a tree while the other scope doesn't

def sigmoid100(argu):
    return 1/(1 + np.exp(- sigma * argu*100))
def sigmoid(argu):
    return (1+np.exp(-0.5)) / (1 + np.exp(0.5 - argu))
def toDot(students):
    # input array of students
    # output to a file
    dot = Digraph(comment='Conformance Graph')
    num_students = len(students)
    # add all references
    #for x in range(num_ref+1):
    #    dot.node("ref_"+str(x), "ref_"+str(x))
    for x in range(num_students):
        if isinstance(students[x], int):
            continue
        if not students[x].isSuspicious():
            # no similar ref
            continue
        with dot.subgraph(name="student_"+str(x)) as c:
            c.node("student_"+str(x), "student_"+str(x))
            #print(students[x].sim_ref)
            for y in range(len(students[x].sim_ref)):
                #print(y)
                #print(len(students[x].sim_ref))
                if students[x].sim_ref[y][1] > overall_threshold:
                    dot.node("ref_"+str(students[x].sim_ref[y][0]), "ref_"+str(students[x].sim_ref[y][0]))
                    c.edge("student_"+str(x), "ref_"+str(students[x].sim_ref[y][0]), label=str(students[x].sim_ref[y][1][0:5]))

    print("Output plagiarism graph at graphviz/plagiarism.pdf")
    dot.render('graphviz/plagiarism', view=True)  
            
             


def compare2tree(tree1, tree2):
        p = subprocess.check_output(['java', '-jar', PATH+'apted.jar', '-f', tree1, tree2])
        # p's format is bytes
        outputStr = p.decode("utf-8")
        if outputStr[0] == 'T':
            return -1
        outputStr = outputStr[0:len(outputStr)-1]
        return float(outputStr)

class student:
    # a student has num_scope scopes
    def __init__(self):
        self.scope = []     # append scopeFile objects to the scope field
        self.conformance=[[-100 for x in range(num_ref+1)] for y in range(num_scope+1)]
        self.sim_ref =[[]]
        self.valid = -1
    def isValid(self):
        if self.valid != 1:
            return False
        if len(self.scope) != num_scope:
            return False
        return True
    def overall(self):
        # calculate overall similarity of this student based on different scopes
        # for each scope
        #       if  
        #           avg(empty_dis, ref_dis) - empty_dis/10 < conformance < avg(empty_dis, ref_dis) - empty_dis/10 
        #           safe
        #
        #   conformance matrix
        #           ref1    ref2    ref3
        #   scp1
        #   scp2
        #   scp3
        for x in range(0, num_scope):
            if len(self.scope) != num_scope:
                print("This student's code doesn't provide a doable tree")
                return 
            scope_obj = self.scope[x] 
            for y in range(1, num_ref+1):
                if scope_obj.similarity[y] == -1:
                    self.conformance[x][y] = default_conformance
                    continue
                # avg = (empty_dis + ref_scope)/2
                avg = (scope_obj.similarity[0] + scope_obj.similarity[y])/2
                bias = scope_obj.similarity[0]/10
                #if scope_obj.similarity[y] > (avg - bias) and scope_obj.similarity[y] < avg + 2*bias:
                #    # safe
                #    self.conformance[x][y]=0
                #else :
                #    # similar
                if True:
                    if SIGMOID==1:
                        similarity = 1 - sigmoid100(scope_obj.similarity[y] / avg )
                    else:
                         similarity = 1 - (scope_obj.similarity[y] /(avg))
                    if similarity <= 0:
                        similarity = 0
                    self.conformance[x][y] = (similarity)
        self.valid = 0
    def listSimilar(self):
        # return a list of int which are ids of similar references
        if not self.scope:
                return 
        guilty_ref = []
        for y in range(1, num_ref+1):
            count=0
            for x in range(0, num_scope):
                if self.conformance[x][y] <= -1:
                    continue
                #print(self.conformance[x][y])
                if self.conformance[x][y] > threshold :
                    #print("Found a similar scope at ref{:d} scope\"{:s} with conformance\"".format(y, scope[x]), end='')
                    #print(self.conformance[x][y])
                    count = count+1
            if count >= 2:
                # calculate weighted avg
                up = 0.0
                down = 0.0
                sim=0
                for c in range(0, num_scope):
                    down = down+self.scope[c].similarity[0]
                #print("Down = {:f}".format(down))
                for c in range(0, num_scope):
                    #print(self.conformance[c][y])
                    up = up+self.conformance[c][y]*self.scope[c].similarity[0]
                # print("Up = {:f}".format(up))

                sim = up/down
                if len(self.sim_ref[0]) == 0:
                    self.sim_ref[len(self.sim_ref)-1].append(y)
                else:
                    self.sim_ref.append([y])
                self.sim_ref[len(self.sim_ref)-1].append(sigmoid(sim))
        self.valid = 1
    def showConformance(self):
        if not self.isValid():
                return 
        if not self.sim_ref[0]:
            print(" no similar reference")
            return 
        print(self.sim_ref[0])
        #    if not self.sim_ref[0]:
        #       print("Not similar to any reference")
        #    else:
        #        for x in range(len(self.sim_ref)):
        #            print("Similar to ref{:d} with similarity{:d}".format(self.sim_ref[x][0], self.sim_ref[x][1]))
    def isSuspicious(self):
        if len(self.sim_ref[0])==0:
            return False
        for x in range(len(self.sim_ref)):
            if self.sim_ref[x][1] > overall_threshold:
                return True
        return False
class scopeFile:
    # one scope corresponds to one file
    def __init__(self, name, studentId, scopeid):
        self.filename=name      # file name
        self.similarity=[0 for y in range(num_ref+1)]
        self.id = studentId
        self.scope = scopeid
    # collect similarity from the references
    #       empty ref1   ref2    ref3    ... 
    # scpe1 
    # empty distance
    def empty_dis(self):
        self.similarity[0] = compare2tree(self.filename, PATH+"brk_tree/empty.tree")

    # array distance from all references
    def add_distance(self):
            for y in range(1, num_ref+1):
                refer_name = PATH+"brk_tree/reference_"+str(y)+"_tsh.c.001t.tu."+scope[self.scope]+".tree"
                if not os.path.exists(refer_name):
                    self.similarity[y] = -1
                    continue
                elif os.stat(refer_name).st_size == 0:
                    self.similarity[y] = -1
                    continue
                self.similarity[y] = compare2tree(self.filename, refer_name)
            #print("Compare student {:d}, scope {:s} with all references".format(self.id, scope[self.scope]))

    def myprint(self):
            for y in range(num_ref+1):  # print to ref 39
                print("(x, y): (0 , "+str(y)+")")
                print((self.similarity[y]))


if __name__ == "__main__":

    with open(PATH+"test/scope.conf") as f:
        content = f.readlines()

    scope = [x.strip() for x in content]
    scope.remove(scope[0])
    num_scope = len(scope)
    students = []
    
    for x in range(0, num_student+1):
        s = student()
        for y in range(0, num_scope):
            s_name = PATH+"brk_tree/"+str(x)+"_tsh.c.001t.tu."+scope[y]+".tree"
            if os.stat(s_name).st_size == 0:
                # student tree does not exist
                students.append(-1)
                break
            scope1=scopeFile(s_name, x, y)
            # compute empty distance
            scope1.empty_dis()
            # compare student's this scope with each reference of this scope
            scope1.add_distance()
            # add student's this scope to object of this student
            s.scope.append(scope1)
        students.append(s)
        print("Student {:d} compared with all references".format(x))
    for x in range(0, len(students)):
    # calculate overall similarity---in terms of each reference
        if students[x] == -1:
            print("Student {:d} fails to compile".format(x))
            continue
        print("Student {:d}: ".format(x), end='')
        students[x].overall()
        students[x].listSimilar()
        students[x].showConformance()
    print("Totally compared num of students "+str(len(students)))
    toDot(students)
    
